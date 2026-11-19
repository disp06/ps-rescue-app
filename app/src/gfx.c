/*
 * gfx.c - tiny double-buffered software renderer straight on the display
 * framebuffer. No GPU, no external libs, no font files: keeps the VPK small
 * and works even when the system is half broken.
 */
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <string.h>
#include "gfx.h"
#include "font_data.h"

#define FB_SIZE   (2 * 1024 * 1024)   /* 960*544*4 rounded to 256 KiB */

static SceUID    g_block[2] = { -1, -1 };
static uint32_t *g_fb[2];
static int       g_back;

int gfx_init(void) {
	for (int i = 0; i < 2; i++) {
		g_block[i] = sceKernelAllocMemBlock("vbd_fb", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
		                                    FB_SIZE, NULL);
		if (g_block[i] < 0) return g_block[i];
		sceKernelGetMemBlockBase(g_block[i], (void **)&g_fb[i]);
		memset(g_fb[i], 0, FB_SIZE);
	}
	g_back = 1;
	gfx_flip();
	return 0;
}

void gfx_end(void) {
	for (int i = 0; i < 2; i++)
		if (g_block[i] >= 0) sceKernelFreeMemBlock(g_block[i]);
}

void gfx_flip(void) {
	SceDisplayFrameBuf fb;
	memset(&fb, 0, sizeof(fb));
	fb.size        = sizeof(fb);
	fb.base        = g_fb[g_back];
	fb.pitch       = SCREEN_W;
	fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
	fb.width       = SCREEN_W;
	fb.height      = SCREEN_H;
	sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
	sceDisplayWaitVblankStart();
	g_back ^= 1;
}

void gfx_clear(uint32_t c) {
	uint32_t *p = g_fb[g_back];
	for (int i = 0; i < SCREEN_W * SCREEN_H; i++) p[i] = c;
}

void gfx_rect(int x, int y, int w, int h, uint32_t c) {
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > SCREEN_W) w = SCREEN_W - x;
	if (y + h > SCREEN_H) h = SCREEN_H - y;
	if (w <= 0 || h <= 0) return;
	for (int j = 0; j < h; j++) {
		uint32_t *row = g_fb[g_back] + (y + j) * SCREEN_W + x;
		for (int i = 0; i < w; i++) row[i] = c;
	}
}

void gfx_frame(int x, int y, int w, int h, uint32_t c) {
	gfx_rect(x, y, w, 2, c);
	gfx_rect(x, y + h - 2, w, 2, c);
	gfx_rect(x, y, 2, h, c);
	gfx_rect(x + w - 2, y, 2, h, c);
}

static inline uint32_t blend(uint32_t dst, uint32_t src, unsigned a) {
	/* ABGR8888, alpha 0..255 */
	uint32_t rb = (((src & 0x00FF00FF) * a) + ((dst & 0x00FF00FF) * (255 - a))) >> 8;
	uint32_t g  = (((src & 0x0000FF00) * a) + ((dst & 0x0000FF00) * (255 - a))) >> 8;
	return 0xFF000000 | (rb & 0x00FF00FF) | (g & 0x0000FF00);
}

static void draw_char(int x, int y, char ch, uint32_t c) {
	if (ch < 32 || ch > 126) ch = '?';
	const unsigned char *g = font_data[ch - 32];
	for (int j = 0; j < FONT_H; j++) {
		int py = y + j;
		if (py < 0 || py >= SCREEN_H) continue;
		uint32_t *row = g_fb[g_back] + py * SCREEN_W;
		for (int i = 0; i < FONT_W; i++) {
			int px = x + i;
			unsigned a = g[j * FONT_W + i];
			if (!a || px < 0 || px >= SCREEN_W) continue;
			row[px] = (a == 255) ? c : blend(row[px], c, a);
		}
	}
}

/* Draws at most max_cols characters; truncates with '~'. Returns cols used. */
int gfx_text_n(int x, int y, const char *s, uint32_t c, int max_cols) {
	int n = 0;
	while (*s && *s != '\n' && *s != '\r') {
		if (max_cols > 0 && n == max_cols - 1 && s[1] && s[1] != '\n') {
			draw_char(x + n * FONT_W, y, '~', c);
			return n + 1;
		}
		char ch = (*s == '\t') ? ' ' : *s;
		draw_char(x + n * FONT_W, y, ch, c);
		s++; n++;
		if (max_cols > 0 && n >= max_cols) break;
	}
	return n;
}

int gfx_text(int x, int y, const char *s, uint32_t c) {
	return gfx_text_n(x, y, s, c, (SCREEN_W - x) / FONT_W);
}
