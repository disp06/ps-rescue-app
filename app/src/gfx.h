#ifndef GFX_H
#define GFX_H
#include <stdint.h>

#define SCREEN_W 960
#define SCREEN_H 544
#define CHAR_W   10
#define CHAR_H   20

/* colours are ABGR8888 */
#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

#define C_BG      RGB(14, 18, 28)
#define C_PANEL   RGB(26, 32, 48)
#define C_SEL     RGB(42, 74, 130)
#define C_TEXT    RGB(220, 226, 236)
#define C_DIM     RGB(130, 140, 160)
#define C_OK      RGB(90, 210, 120)
#define C_WARN    RGB(240, 190, 60)
#define C_BAD     RGB(240, 90, 80)
#define C_ACCENT  RGB(90, 170, 255)
#define C_BLACK   RGB(0, 0, 0)

int  gfx_init(void);
void gfx_end(void);
void gfx_flip(void);
void gfx_clear(uint32_t c);
void gfx_rect(int x, int y, int w, int h, uint32_t c);
void gfx_frame(int x, int y, int w, int h, uint32_t c);
int  gfx_text(int x, int y, const char *s, uint32_t c);
int  gfx_text_n(int x, int y, const char *s, uint32_t c, int max_cols);

#endif
