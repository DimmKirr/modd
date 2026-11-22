#ifndef UI_PALETTE_H
#define UI_PALETTE_H
#include <stdint.h>
typedef struct { const char *name; uint16_t bg, surf, text, accent, trk[4], knob[4][4]; } ui_pal_t;
#endif
