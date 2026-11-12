#include "rf5c_model.h"
void rf5c_key(Rf5c *c, int ch, int on) { Rf5cCh *h = &c->ch[ch]; if (on && !h->on) h->addr = (uint32_t)h->start << (8 + 11); h->on = (uint8_t)on; }
void rf5c_sample(Rf5c *c, int32_t *l, int32_t *r)
{
    int32_t L = 0, R = 0;
    for (int i = 0; i < 8; i++) {
        Rf5cCh *h = &c->ch[i]; if (!h->on) continue;
        const int lv = (h->pan & 15) * h->env, rv = (h->pan >> 4) * h->env;
        int s = c->ram[(h->addr >> 11) & 0xFFFF];
        if (s == 0xFF) { h->addr = (uint32_t)h->loopst << 11; s = c->ram[(h->addr >> 11) & 0xFFFF]; if (s == 0xFF) continue; }
        h->addr += h->step;
        if (s & 0x80) { s &= 0x7F; L += (s * lv) >> 5; R += (s * rv) >> 5; } else { L -= (s * lv) >> 5; R -= (s * rv) >> 5; }
    }
    if (L > 32767) L = 32767;
    if (L < -32768) L = -32768;
    if (R > 32767) R = 32767;
    if (R < -32768) R = -32768;
    *l = L & ~0x3F; *r = R & ~0x3F;
}
