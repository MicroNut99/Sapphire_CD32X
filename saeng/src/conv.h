/* conv.h - DIAG, docs/45 (conv.c) */
#include <stdint.h>
void conv_init(void);
void conv_tile(const uint16_t *t, uint32_t *out);
void conv_cell(const uint16_t *c, uint32_t *out);
