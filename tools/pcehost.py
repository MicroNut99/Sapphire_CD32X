"""pcehost.py (from DraculaX_32X, with the Arcade Card exports added for Sapphire) - a minimal headless libretro host for Mednafen's PC Engine core, for graphics capture.
    h = Host(core_so, cue, sysdir); h.run(n, buttons); h.screenshot(path); cap = h.capture()"""
import sys, ctypes as C, numpy as np
from PIL import Image

ENV_GET_SYSTEM_DIRECTORY, ENV_SET_PIXEL_FORMAT, ENV_GET_VARIABLE, ENV_GET_VARIABLE_UPDATE = 9, 10, 15, 17
ENV_GET_SAVE_DIRECTORY = 31
BTN = dict(B=0, Y=1, SELECT=2, START=3, UP=4, DOWN=5, LEFT=6, RIGHT=7, A=8, X=9)   # PCE: II = B, I = A, RUN = START

class Var(C.Structure): _fields_ = [('key', C.c_char_p), ('value', C.c_char_p)]
class GameInfo(C.Structure): _fields_ = [('path', C.c_char_p), ('data', C.c_void_p), ('size', C.c_size_t), ('meta', C.c_char_p)]

ENV_T = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO_T = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
SAMPLE_T = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
BATCH_T = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
POLL_T = C.CFUNCTYPE(None)
STATE_T = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)

OPTIONS = {b'pce_cdbios': b'System Card 3', b'pce_cdimagecache': b'disabled', b'pce_arcadecard': b'enabled'}

class Host:
    def __init__(self, core, cue, sysdir):
        self.lib = C.CDLL(core)
        self._sysbuf = C.create_string_buffer(sysdir.encode())        # C-owned for the host's life
        self._optbuf = {k: C.create_string_buffer(v) for k, v in OPTIONS.items()}
        self.frame = None; self.held = set(); self.frames = 0
        self.inputs = None          # a list: every frame's buttons (a bit mask of BTN) is appended (record())
        self._keep = []
        # retro_log_printf_t is variadic; only the level and format are read (enough for a trace)
        self._logcb = C.CFUNCTYPE(None, C.c_int, C.c_char_p)(
            lambda lvl, fmt: lvl >= 2 and sys.stderr.write('[core] ' + fmt.decode(errors='replace')))
        def env(cmd, data):
            cmd &= 0xFFFF
            if cmd in (ENV_GET_SYSTEM_DIRECTORY, ENV_GET_SAVE_DIRECTORY):
                C.cast(data, C.POINTER(C.c_void_p))[0] = C.addressof(self._sysbuf); return True
            if cmd == ENV_SET_PIXEL_FORMAT:
                return C.cast(data, C.POINTER(C.c_int))[0] == 2      # RGB565 only
            if cmd == ENV_GET_VARIABLE:
                key = C.cast(data, C.POINTER(C.c_char_p))[0]
                if key in self._optbuf:
                    C.cast(data, C.POINTER(C.c_void_p))[1] = C.addressof(self._optbuf[key]); return True
                return False
            if cmd == 27:                                            # GET_LOG_INTERFACE
                C.cast(data, C.POINTER(C.c_void_p))[0] = C.cast(self._logcb, C.c_void_p).value; return True
            if cmd == ENV_GET_VARIABLE_UPDATE:
                C.cast(data, C.POINTER(C.c_bool))[0] = False; return True
            return False
        def video(data, w, h, pitch):
            if data:
                a = np.ctypeslib.as_array(C.cast(data, C.POINTER(C.c_uint16)), (h * pitch // 2,))
                self.frame = a.reshape(h, pitch // 2)[:, :w].copy()
        self.cbs = [ENV_T(env), VIDEO_T(video), SAMPLE_T(lambda l, r: None), BATCH_T(lambda d, n: n),
                    POLL_T(lambda: None), STATE_T(lambda p, dev, i, id: int(p == 0 and dev == 1 and id in self.held))]
        L = self.lib
        L.retro_set_environment(self.cbs[0]); L.retro_set_video_refresh(self.cbs[1])
        L.retro_set_audio_sample(self.cbs[2]); L.retro_set_audio_sample_batch(self.cbs[3])
        L.retro_set_input_poll(self.cbs[4]); L.retro_set_input_state(self.cbs[5])
        L.retro_init()
        gi = GameInfo(cue.encode(), None, 0, None)
        if not L.retro_load_game(C.byref(gi)):
            raise RuntimeError('retro_load_game failed')
        L.retro_set_controller_port_device(0, 1)

    def run(self, n=1, *buttons):
        self.held = {BTN[b] for b in buttons}
        for _ in range(n):
            self._step()
        self.held = set()

    def _step(self):
        if hasattr(self.lib, 'dx_setframe'): self.lib.dx_setframe(self.frames)
        if self.inputs is not None: self.inputs.append(sum(1 << b for b in self.held))
        self.lib.retro_run(); self.frames += 1

    # ---- M0b: input record / replay. The core reads the pad once a frame, so a frame's mask is all of it ----
    def record(self):
        """start recording: from now on every frame's buttons go into self.inputs"""
        self.inputs = []

    def play(self, masks, each=None):
        """replay recorded masks, one a frame; each(host) is called after every frame"""
        for m in masks:
            self.held = {b for b in range(16) if m >> b & 1}
            self._step()
            if each: each(self)
        self.held = set()

    def sound_log(self, clear=True):
        """M0b: the sound log since the last call, an (n, 8) uint32 array (core dumpvid.cpp, sndtrack)"""
        n = self.lib.sndtrack_get(None, 0, 0)
        buf = (C.c_uint32 * (8 * min(n, 1 << 20)))()
        self.lib.sndtrack_get(buf, min(n, 1 << 20), int(clear))
        if n > 1 << 20: raise RuntimeError('sound log overflow: %d entries; read it more often' % n)
        return np.array(buf, np.uint32).reshape(-1, 8)

    def adpcm_ram(self):
        b = (C.c_uint8 * 65536)(); self.lib.adpcm_ram(b); return np.array(b, np.uint8)

    def tap(self, *buttons, hold=4, after=20):
        self.run(hold, *buttons); self.run(after)

    def screenshot(self, path):
        f = self.frame.astype(np.uint32)
        rgb = np.dstack([((f >> 11) & 31) * 255 // 31, ((f >> 5) & 63) * 255 // 63, (f & 31) * 255 // 31]).astype(np.uint8)
        Image.fromarray(rgb).save(path)

    def capture(self):
        vram = (C.c_uint16 * 65536)(); sat = (C.c_uint16 * 256)(); pal = (C.c_uint16 * 512)(); regs = (C.c_uint16 * 8)()
        if not self.lib.dumpvid(vram, sat, pal, regs): raise RuntimeError('dumpvid: no VCE')
        out = dict(vram=np.array(vram, np.uint16), sat=np.array(sat, np.uint16),
                   pal=np.array(pal, np.uint16), regs=np.array(regs, np.uint16))
        if hasattr(self.lib, 'dumpvid0'):          # M1b: VRAM / sprite table at the first display line
            v0 = (C.c_uint16 * 65536)(); s0 = (C.c_uint16 * 256)(); p0 = (C.c_uint16 * 512)()
            self.lib.dumpvid0(v0, s0, p0)
            out['vram0'] = np.array(v0, np.uint16); out['sat0'] = np.array(s0, np.uint16)
            out['pal0'] = np.array(p0, np.uint16)
        return out

    # ---- Sapphire: the Arcade Card (core/sapphire.cpp) ----
    def ac_track(self, clear=True):
        """flags per byte of the Arcade Card's 2 MB: 1 BIOS wrote, 2 game wrote, 4 game read (port reads)"""
        b = (C.c_uint8 * 0x200000)(); self.lib.actrack_get(b, int(clear)); return np.frombuffer(bytes(b), np.uint8)

    def ac_stat(self, clear=True):
        o = (C.c_uint64 * 2)(); self.lib.acstat_get(o, int(clear)); return int(o[0]), int(o[1])

    def ac_ram(self):
        b = (C.c_uint8 * 0x200000)(); self.lib.acram_copy(b); return np.frombuffer(bytes(b), np.uint8)

    def ram(self):
        self.lib.retro_get_memory_data.restype = C.c_void_p
        p = self.lib.retro_get_memory_data(2); n = self.lib.retro_get_memory_size(2)
        return np.ctypeslib.as_array(C.cast(p, C.POINTER(C.c_uint8)), (n,))

    def save_state(self, path=None):
        n = self.lib.retro_serialize_size(); buf = (C.c_uint8 * n)()
        if not self.lib.retro_serialize(buf, n): raise RuntimeError('serialize failed')
        b = bytes(buf)
        if path: open(path, 'wb').write(b)
        return b

    def load_state(self, b):
        if isinstance(b, str): b = open(b, 'rb').read()
        buf = (C.c_uint8 * len(b)).from_buffer_copy(b)
        if not self.lib.retro_unserialize(buf, len(b)): raise RuntimeError('unserialize failed')
