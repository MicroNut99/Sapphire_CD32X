# 94. v29: the intro's narration in English (20 Nov 2026)

**State: untested on the console.** The owner: "There is an intro scene with all japanese language. that needs to be
translated".

## What the intro's Japanese is
- On screen, none: the original's pictures every 60 frames from 600 to the title (recording playL; the PC machine,
  `RUN_DUMP_VIDEO`) are the loading, the city, Nemesis, the commander, the airships, the pilots, ALERT, the launch.
- **Spoken**: `AUDIO_LOG` - the game plays CD track 15 (LBA 144,633, 87.6 s) at frame 1,348, the title's track at 6,577;
  no ADPCM between. The work folder's `disc/a15.bin` is silence (a stand-in); the track is the uploads' `Session 1/Track
  15.wav` (its first 2 s the pregap).
- Transcribed here: sherpa-onnx 2.0.2 (PyPI) with its SenseVoice model and silero VAD (GitHub releases; nothing of
  them in the repository): one narrator, 0.55-53.41 s, twelve phrases; the last 34 s music. The recogniser's slips
  mended by reading (反重力物質 "anti-gravity matter", 人類積年の夢, 非合法, 時間渡航禁止法案,
  対時間渡航犯罪捜査局). The English is this round's own translation, in the work folder's `subs_intro.txt`
  ("<from s> <to s>|<row>|<row>", seconds into the track), made into `subs_intro.h` by `tools/saeng_disc.sh`.

## v29
- `eng.c eng_audio`: as the game starts track 15 (`SAP_INTRO_LBA`), the 32X's blank count is kept (`gTicks`: real
  time, as the CD plays - the picture's pace does not move the words); another play or a stop ends it.
- `sap_tick`: the line whose span holds the time since, while ENGLISH TEXT is on.
- `menu.c gm_subs` mode 2: plain white at the bottom edge (the intro's pictures are letterboxed: black there), drawn on
  every picture (`main.c`) - the 32X draws the intro's whole picture (docs/82) and wiped letters between v28's 2-in-8
  drawings (ares, the first build: "WEARY OF IT", ".ME Y / ERIS").
ares: the twelve lines whole in every picture from "A.D. 2092." over the city to "UNTIL AT LAST THE TIME CRIME /
INVESTIGATION BUREAU WAS BORN." over the pilots' emblem; the intro's pace v28's (6,900 = game 6,428; v28 6,441).
