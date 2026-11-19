/*
 * VitaBootDiag - PS Vita boot rescue & diagnostics
 *
 * Controls:
 *   D-Pad / left stick  navigate        L / R      switch tab
 *   X                   select / apply  O          cancel / back
 *   Triangle            raw boot log    Square     export report
 *   START               exit
 */
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/power.h>
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "fsutil.h"
#include "config.h"
#include "detect.h"
#include "errdict.h"
#include "fixes.h"
#include "report.h"

#define APP_VERSION "1.0"
#define CFG_PATH    "ur0:tai/config.txt"

#define TOP      44
#define ROWS     22                 /* content rows (20 px each) */
#define COLS     94
#define LEFT     10

/* ------------------------------------------------------------ state --- */

enum { TAB_OVERVIEW, TAB_PLUGINS, TAB_FIXES, TAB_DUMPS, TAB_ERRORS, TAB_COUNT };
static const char *TAB_NAME[TAB_COUNT] = { "Overview", "Plugins", "Fixes", "Dumps", "Errors" };

enum { V_MAIN, V_LOG, V_DETAIL, V_CONFIRM, V_MSG, V_PICKDRV };

enum {
	ACT_NONE, ACT_FIX1, ACT_FIX2, ACT_FIX3, ACT_FIX4, ACT_REBOOT,
	ACT_IDDAT, ACT_APPDB, ACT_LOG_INSTALL, ACT_LOG_UNINSTALL, ACT_RESCAN
};

static Config     g_cfg;
static DeviceInfo g_dev;
static int        g_tab, g_view = V_MAIN;
static int        g_sel[TAB_COUNT], g_scroll[TAB_COUNT];
static int        g_dirty = 1;

/* confirm / message */
static char g_box_title[96];
static char g_box_body[2048];
static int  g_box_action;
static int  g_msg_offer_reboot;
static int  g_pick_sel, g_pick_drv[4], g_pick_n;

/* log viewer */
static char  *g_log_buf;
static char **g_log_lines;
static int    g_log_n, g_log_top, g_log_hscroll;

/* error detail */
static int g_detail_idx;

/* ------------------------------------------------------------ input --- */

typedef struct { uint32_t pressed, held; } Input;

static Input read_input(void) {
	static uint32_t prev = 0;
	static int rep_timer = 0;
	SceCtrlData pad;
	memset(&pad, 0, sizeof(pad));
	sceCtrlPeekBufferPositive(0, &pad, 1);
	uint32_t b = pad.buttons;
	/* left stick -> d-pad */
	if (pad.ly < 50)  b |= SCE_CTRL_UP;
	if (pad.ly > 205) b |= SCE_CTRL_DOWN;
	if (pad.lx < 50)  b |= SCE_CTRL_LEFT;
	if (pad.lx > 205) b |= SCE_CTRL_RIGHT;

	Input in;
	in.held = b;
	in.pressed = b & ~prev;
	/* auto-repeat for directions */
	uint32_t dirs = SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT;
	if (b & dirs & prev) {
		if (++rep_timer > 20 && (rep_timer % 4) == 0) in.pressed |= b & dirs;
	} else rep_timer = 0;
	prev = b;
	return in;
}

/* ---------------------------------------------------------- helpers --- */

static void rescan(void) {
	cfg_free(&g_cfg);
	cfg_load(&g_cfg, CFG_PATH);
	detect_all(&g_dev, &g_cfg);
	g_dirty = 1;
}

static void open_confirm(int action, const char *title, const char *body) {
	g_box_action = action;
	snprintf(g_box_title, sizeof(g_box_title), "%s", title);
	snprintf(g_box_body, sizeof(g_box_body), "%s", body);
	g_view = V_CONFIRM;
}

static void open_msg(const char *title, const char *body, int offer_reboot) {
	snprintf(g_box_title, sizeof(g_box_title), "%s", title);
	snprintf(g_box_body, sizeof(g_box_body), "%s", body);
	g_msg_offer_reboot = offer_reboot;
	g_view = V_MSG;
}

/* word-wraps 'text' into a box; returns rows drawn */
static int draw_wrapped(int x, int y, const char *text, uint32_t col, int cols, int max_rows) {
	int rows = 0;
	const char *p = text;
	char line[COLS + 2];
	while (*p && rows < max_rows) {
		if (*p == '\n') { p++; rows++; continue; }
		int n = 0, last_space = -1;
		while (p[n] && p[n] != '\n' && n < cols) { if (p[n] == ' ') last_space = n; n++; }
		int take = n;
		if (p[n] && p[n] != '\n' && last_space > 0) take = last_space;
		memcpy(line, p, take);
		line[take] = 0;
		gfx_text_n(x, y + rows * CHAR_H, line, col, cols);
		rows++;
		p += take;
		while (*p == ' ') p++;
		if (*p == '\n') p++;
	}
	return rows;
}

static void free_log(void) {
	free(g_log_buf); free(g_log_lines);
	g_log_buf = NULL; g_log_lines = NULL; g_log_n = 0;
}

static void load_log(void) {
	free_log();
	int len = 0;
	g_log_buf = fs_read_all(BD_LOG, &len);
	if (!g_log_buf) {
		g_log_buf = strdup("(no boot log yet)\n\nInstall the boot logger from the Fixes tab,\n"
		                   "or place it manually (see README), then reboot.\n");
		len = strlen(g_log_buf);
	}
	int cap = 1;
	for (int i = 0; i < len; i++) if (g_log_buf[i] == '\n') cap++;
	g_log_lines = malloc(cap * sizeof(char *));
	char *p = g_log_buf;
	while (p && *p && g_log_n < cap) {
		g_log_lines[g_log_n++] = p;
		char *nl = strchr(p, '\n');
		if (!nl) break;
		*nl = 0;
		if (nl > p && nl[-1] == '\r') nl[-1] = 0;
		p = nl + 1;
	}
	g_log_top = g_log_n > ROWS ? g_log_n - ROWS : 0;
	g_log_hscroll = 0;
}

static uint32_t status_col(CfgStatus s) {
	switch (s) {
	case CS_OK:     return C_OK;
	case CS_NODEV:
	case CS_ODD_EXT: return C_WARN;
	default:        return C_BAD;
	}
}

/* ------------------------------------------------------- tab: overview --- */

static int ov_line(int row, const char *label, const char *val, uint32_t col) {
	int y = TOP + row * CHAR_H;
	gfx_text(LEFT, y, label, C_DIM);
	gfx_text_n(LEFT + 21 * CHAR_W, y, val, col, COLS - 21);
	return row + 1;
}

static void draw_overview(void) {
	char b[160], h1[32], h2[32], h3[32];
	int r = 0;
	r = ov_line(r, "Console", g_dev.model, C_TEXT);
	r = ov_line(r, "Firmware (reported)", g_dev.fw, C_TEXT);
	r = ov_line(r, "3G modem", g_dev.has_3g ? "present" : "not present", C_TEXT);
	snprintf(b, sizeof(b), "ux0:%s  uma0:%s  imc0:%s  grw0:%s  xmc0:%s",
	         g_dev.ux0 ? "OK" : "--", g_dev.uma0 ? "OK" : "--", g_dev.imc0 ? "OK" : "--",
	         g_dev.grw0 ? "OK" : "--", g_dev.xmc0 ? "OK" : "--");
	r = ov_line(r, "Mounted devices", b, g_dev.ux0 ? C_TEXT : C_BAD);
	snprintf(b, sizeof(b), "ux0 %s free of %s   ur0 %s free",
	         fs_human_size(g_dev.ux0_free, h1, 32), fs_human_size(g_dev.ux0_max, h2, 32),
	         fs_human_size(g_dev.ur0_free, h3, 32));
	r = ov_line(r, "Free space", b, C_TEXT);
	r = ov_line(r, "SD2Vita / storage", g_dev.sd2vita,
	            g_dev.drivers_active > 1 ? C_BAD : (g_dev.drivers_active && !g_dev.ux0) ? C_BAD : C_TEXT);
	snprintf(b, sizeof(b), "%d entries, %d broken, %d on unmounted device",
	         g_dev.entries, g_dev.broken, g_dev.nodev);
	r = ov_line(r, "ur0:tai/config.txt", g_dev.ur0_cfg ? b : "MISSING",
	            !g_dev.ur0_cfg || g_dev.broken ? C_BAD : C_OK);
	snprintf(b, sizeof(b), "kernel %s / shell %s",
	         g_dev.logger_k ? "installed" : "not installed", g_dev.logger_u ? "installed" : "not installed");
	r = ov_line(r, "Boot logger", b, (g_dev.logger_k && g_dev.logger_u) ? C_OK : C_WARN);
	uint32_t hint = 0;
	const char *v = detect_boot_verdict(&g_dev, &hint);
	r = ov_line(r, "Last boot", v, hint == 1 ? C_OK : hint == 2 ? C_BAD : C_WARN);
	if (g_dev.state_ok) {
		snprintf(b, sizeof(b), "#%u  (failed boots in a row before this one: %u)",
		         (unsigned)g_dev.state.boot_count, (unsigned)g_dev.state.fails_in_row);
		r = ov_line(r, "Boot counter", b, C_TEXT);
	}

	r++;
	gfx_text(LEFT, TOP + r * CHAR_H, "Detected problems:", C_ACCENT);
	r++;
	int issues = 0;
#define ISSUE(cond, txt) do { if ((cond) && r < ROWS) { \
	gfx_text_n(LEFT + 20, TOP + r * CHAR_H, "- " txt, C_WARN, COLS - 2); r++; issues++; } } while (0)
	ISSUE(!g_dev.ur0_cfg, "ur0:tai/config.txt is missing");
	ISSUE(g_dev.ur0_cfg && g_dev.ux0_cfg, "ux0:tai/config.txt overrides ur0:tai/config.txt  -> Fix 1");
	ISSUE(g_dev.broken > 0, "config.txt has missing / misplaced plugin entries        -> Fix 2");
	ISSUE(g_dev.drivers_active > 1, "more than one SD2Vita/storage driver is enabled         -> Fix 3");
	ISSUE(g_dev.drivers_active && !g_dev.ux0, "storage driver configured but ux0: is not mounted");
	ISSUE(g_dev.early_dev > 0, "*KERNEL plugin(s) not on ur0: (may load before ux0 mounts)");
	ISSUE(g_dumps.count > 0, "crash dumps present (see Dumps tab)                     -> Fix 4");
	ISSUE(g_dev.state_ok && g_dev.state.prev_stage == BD_STAGE_KERNEL,
	      "a previous boot never reached SceShell (Triangle = log)");
	ISSUE(g_dev.ux0 && g_dev.ux0_max && g_dev.ux0_free < (64ull << 20), "ux0: has less than 64 MB free");
#undef ISSUE
	if (!issues) gfx_text(LEFT + 20, TOP + r * CHAR_H, "none - looks healthy", C_OK);
}

/* -------------------------------------------------------- tab: plugins --- */

static int plugin_rows(void) { return g_cfg.loaded ? g_cfg.count : 0; }

static void draw_plugins(void) {
	if (!g_cfg.loaded) {
		gfx_text(LEFT, TOP, "ur0:tai/config.txt could not be read.", C_BAD);
		return;
	}
	int n = g_cfg.count;
	int *sel = &g_sel[TAB_PLUGINS], *top = &g_scroll[TAB_PLUGINS];
	if (*sel >= n) *sel = n - 1;
	if (*sel < 0) *sel = 0;
	if (*sel < *top) *top = *sel;
	if (*sel >= *top + ROWS - 2) *top = *sel - (ROWS - 3);
	for (int i = 0; i < ROWS - 2 && *top + i < n; i++) {
		const CfgLine *l = &g_cfg.lines[*top + i];
		int y = TOP + i * CHAR_H;
		if (*top + i == *sel) gfx_rect(0, y, SCREEN_W, CHAR_H, C_SEL);
		switch (l->type) {
		case CL_SECTION: gfx_text_n(LEFT, y, l->text, C_ACCENT, COLS); break;
		case CL_COMMENT: gfx_text_n(LEFT + 20, y, l->text, C_DIM, COLS - 2); break;
		case CL_BLANK:   break;
		case CL_ENTRY: {
			char st[20];
			snprintf(st, sizeof(st), "%-13s", cfg_status_name(l->status));
			gfx_text(LEFT + 20, y, st, status_col(l->status));
			gfx_text_n(LEFT + 20 + 14 * CHAR_W, y, l->path,
			           l->warn_early_dev ? C_WARN : C_TEXT, COLS - 16);
			break; }
		}
	}
	const CfgLine *cur = &g_cfg.lines[*sel];
	int y = TOP + (ROWS - 1) * CHAR_H;
	gfx_rect(0, y - 4, SCREEN_W, 2, C_PANEL);
	if (cur->type == CL_ENTRY) {
		char b[200];
		const char *hint =
			cur->status == CS_MISSING ? "file not found - Fix 2 comments it out" :
			cur->status == CS_NODEV ? "device not mounted right now (left alone by Fix 2)" :
			cur->status == CS_WRONG_SECTION ? ".skprx belongs in *KERNEL, .suprx elsewhere" :
			cur->status == CS_NO_SECTION ? "entry before any *SECTION line" :
			cur->status == CS_BAD_PATH ? "path has no device prefix (ur0:/ux0:)" :
			cur->warn_early_dev ? "kernel plugin not on ur0: - move it to ur0:tai" : "ok";
		snprintf(b, sizeof(b), "[%s] %s", cur->section, hint);
		gfx_text_n(LEFT, y, b, C_DIM, COLS);
	} else {
		gfx_text(LEFT, y, "Sections: *KERNEL = kernel plugins, *main = SceShell, *TITLEID = per game", C_DIM);
	}
}

/* ---------------------------------------------------------- tab: fixes --- */

#define FIX_ITEMS 10

static void fix_label(int i, char *b, int bs, char *st, int ss, uint32_t *col) {
	char h[32];
	*col = C_DIM;
	st[0] = 0;
	switch (i) {
	case 0: snprintf(b, bs, "Fix 1   Remove ux0:tai conflict (rename to ux0:tai_backup)");
	        if (fix1_needed(&g_dev)) { snprintf(st, ss, "NEEDED"); *col = C_WARN; }
	        else snprintf(st, ss, "ok");
	        break;
	case 1: snprintf(b, bs, "Fix 2   Comment out missing / broken plugin lines");
	        if (g_dev.broken) { snprintf(st, ss, "%d found", g_dev.broken); *col = C_WARN; }
	        else snprintf(st, ss, "ok");
	        break;
	case 2: snprintf(b, bs, "Fix 3   Resolve SD2Vita driver conflict (YAMT vs StorageMgr)");
	        if (g_dev.drivers_active > 1) { snprintf(st, ss, "%d drivers", g_dev.drivers_active); *col = C_BAD; }
	        else snprintf(st, ss, "ok");
	        break;
	case 3: snprintf(b, bs, "Fix 4   Delete crash dumps (*.psp2dmp in ux0:data, ur0:data)");
	        if (g_dumps.count) { snprintf(st, ss, "%d / %s", g_dumps.count, fs_human_size(g_dumps.total, h, 32)); *col = C_WARN; }
	        else snprintf(st, ss, "none");
	        break;
	case 4: snprintf(b, bs, "Fix 5a  Cold reboot now"); break;
	case 5: snprintf(b, bs, "Fix 5b  Re-index memory card (rename ux0:id.dat) + reboot"); break;
	case 6: snprintf(b, bs, "Fix 5c  Rebuild LiveArea database (rename app.db) + reboot"); break;
	case 7: snprintf(b, bs, "Install boot logger plugins into ur0:tai + config.txt");
	        if (g_dev.logger_k && g_dev.logger_u) { snprintf(st, ss, "installed"); *col = C_OK; }
	        break;
	case 8: snprintf(b, bs, "Uninstall boot logger plugins"); break;
	case 9: snprintf(b, bs, "Rescan everything"); break;
	}
}

static void draw_fixes(void) {
	char b[120], st[40];
	uint32_t col;
	for (int i = 0; i < FIX_ITEMS; i++) {
		int y = TOP + i * (CHAR_H + 6);
		if (i == g_sel[TAB_FIXES]) gfx_rect(0, y - 3, SCREEN_W, CHAR_H + 6, C_SEL);
		fix_label(i, b, sizeof(b), st, sizeof(st), &col);
		gfx_text_n(LEFT, y, b, C_TEXT, 76);
		gfx_text_n(SCREEN_W - 17 * CHAR_W, y, st, col, 16);
	}
	int y = TOP + FIX_ITEMS * (CHAR_H + 6) + 12;
	draw_wrapped(LEFT, y,
		"Every config.txt edit makes a timestamped backup next to it first. Nothing is ever "
		"written outside ur0:/ux0:. Renames are used instead of deletes wherever data could matter.",
		C_DIM, COLS, 4);
}

static void fixes_activate(int i) {
	char body[2048];
	int n = 0;
	switch (i) {
	case 0:
		if (!fix1_needed(&g_dev)) { open_msg("Fix 1", "No conflict: ux0:tai/config.txt does not exist alongside ur0:tai/config.txt.", 0); return; }
		open_confirm(ACT_FIX1, "Fix 1 - remove ux0:tai conflict",
			"ux0:tai/ will be renamed to ux0:tai_backup (nothing is deleted).\n\n"
			"After this taiHEN reads ur0:tai/config.txt only. If ux0:tai held plugins you use, "
			"copy their lines into ur0:tai/config.txt afterwards.\n\nX = apply   O = cancel");
		return;
	case 1:
		if (!g_dev.broken) { open_msg("Fix 2", "No missing or misplaced plugin lines found.", 0); return; }
		n = snprintf(body, sizeof(body), "These lines will be commented out:\n\n");
		for (int k = 0; k < g_cfg.count && n < (int)sizeof(body) - 200; k++) {
			const CfgLine *l = &g_cfg.lines[k];
			if (!cfg_is_broken(l)) continue;
			n += snprintf(body + n, sizeof(body) - n, "[%s] %s  (%s)\n",
			              l->section, l->path, cfg_status_name(l->status));
		}
		snprintf(body + n, sizeof(body) - n, "\nLines on unmounted devices are left alone.\nX = apply   O = cancel");
		open_confirm(ACT_FIX2, "Fix 2 - disable broken plugin lines", body);
		return;
	case 2: {
		int mask = fix3_active_mask(&g_cfg);
		g_pick_n = 0;
		for (int k = 0; k < DRV_COUNT; k++) if (mask & (1 << k)) g_pick_drv[g_pick_n++] = k;
		if (g_pick_n < 2) { open_msg("Fix 3", "Only one (or no) storage driver is enabled. No conflict.", 0); return; }
		g_pick_sel = 0;
		g_view = V_PICKDRV;
		return; }
	case 3: {
		if (!g_dumps.count) { open_msg("Fix 4", "No .psp2dmp crash dumps found.", 0); return; }
		char h[32];
		snprintf(body, sizeof(body), "Delete %d crash dump(s) (%s) from ux0:data and ur0:data?\n\n"
		         "Tip: Square exports a report listing them first.\n\nX = delete   O = cancel",
		         g_dumps.count, fs_human_size(g_dumps.total, h, 32));
		open_confirm(ACT_FIX4, "Fix 4 - clean crash dumps", body);
		return; }
	case 4:
		open_confirm(ACT_REBOOT, "Fix 5a - cold reboot", "Cold-reboot the console now?\n\nX = reboot   O = cancel");
		return;
	case 5:
		open_confirm(ACT_IDDAT, "Fix 5b - re-index memory card",
			"ux0:id.dat will be renamed (backup kept). On the next boot the system re-indexes the "
			"memory card. Useful when the card is not recognised or content looks out of sync.\n\n"
			"X = apply   O = cancel");
		return;
	case 6:
		open_confirm(ACT_APPDB, "Fix 5c - rebuild LiveArea database",
			"ur0:shell/db/app.db will be renamed (backup kept). The system rebuilds it on next boot.\n\n"
			"You WILL lose LiveArea folders and bubble layout. Homebrew bubbles may need VitaShell "
			"'Refresh LiveArea' afterwards. Try that first if you have not.\n\nX = apply   O = cancel");
		return;
	case 7:
		open_confirm(ACT_LOG_INSTALL, "Install boot logger",
			"Copies boot_logger.skprx and boot_logger.suprx to ur0:tai/ and adds:\n\n"
			"  ur0:tai/boot_logger.skprx   as FIRST line under *KERNEL\n"
			"  ur0:tai/boot_logger.suprx   as LAST line under *main\n\n"
			"Logs go to ur0:data/VitaBootDiag/boot.log. config.txt is backed up first.\n\n"
			"X = install   O = cancel");
		return;
	case 8:
		open_confirm(ACT_LOG_UNINSTALL, "Uninstall boot logger",
			"Removes the boot_logger lines from config.txt (backup first) and deletes the two "
			"plugin files from ur0:tai. Logs are kept.\n\nX = uninstall   O = cancel");
		return;
	case 9:
		rescan();
		open_msg("Rescan", "Rescanned config, storage and crash dumps.", 0);
		return;
	}
}

static void run_action(int act) {
	char msg[512] = "";
	int r = 0, reboot_offer = 0;
	switch (act) {
	case ACT_FIX1: r = fix1_apply(msg, sizeof(msg)); reboot_offer = (r >= 0); break;
	case ACT_FIX2: r = fix2_apply(&g_cfg, msg, sizeof(msg)); reboot_offer = (r >= 0); break;
	case ACT_FIX4: r = fix4_apply(msg, sizeof(msg)); break;
	case ACT_REBOOT: fix5_cold_reboot(); return;
	case ACT_IDDAT: r = fix5_refresh_iddat(msg, sizeof(msg)); reboot_offer = (r >= 0); break;
	case ACT_APPDB: r = fix5_rebuild_appdb(msg, sizeof(msg)); reboot_offer = (r >= 0); break;
	case ACT_LOG_INSTALL: r = logger_install(&g_cfg, msg, sizeof(msg)); reboot_offer = (r >= 0); break;
	case ACT_LOG_UNINSTALL: r = logger_uninstall(&g_cfg, msg, sizeof(msg)); break;
	}
	rescan();
	if (reboot_offer) strncat(msg, "\n\nX = reboot now   O = later", sizeof(msg) - strlen(msg) - 1);
	open_msg(r < 0 ? "Failed" : "Done", msg, reboot_offer);
}

/* ---------------------------------------------------------- tab: dumps --- */

static void draw_dumps(void) {
	char h[32], b[200];
	if (!g_dumps.count) {
		gfx_text(LEFT, TOP, "No .psp2dmp crash dumps in ux0:data or ur0:data.", C_OK);
		return;
	}
	snprintf(b, sizeof(b), "%d dump(s), %s total.  X = delete all (Fix 4)", g_dumps.count,
	         fs_human_size(g_dumps.total, h, 32));
	gfx_text(LEFT, TOP, b, C_WARN);
	int *sel = &g_sel[TAB_DUMPS], *top = &g_scroll[TAB_DUMPS];
	if (*sel >= g_dumps.count) *sel = g_dumps.count - 1;
	if (*sel < *top) *top = *sel;
	if (*sel >= *top + ROWS - 4) *top = *sel - (ROWS - 5);
	for (int i = 0; i < ROWS - 4 && *top + i < g_dumps.count; i++) {
		const DumpEntry *e = &g_dumps.items[*top + i];
		int y = TOP + (i + 2) * CHAR_H;
		if (*top + i == *sel) gfx_rect(0, y, SCREEN_W, CHAR_H, C_SEL);
		gfx_text_n(LEFT, y, e->path, C_TEXT, 80);
		gfx_text(SCREEN_W - 12 * CHAR_W, y, fs_human_size(e->size, h, 32), C_DIM);
	}
	gfx_text_n(LEFT, TOP + (ROWS - 1) * CHAR_H,
	           "File name holds the crashed module (e.g. ...-eboot.bin.psp2dmp). Error C2-12828-1 = app crash.",
	           C_DIM, COLS);
}

/* --------------------------------------------------------- tab: errors --- */

static void draw_errors(void) {
	int *sel = &g_sel[TAB_ERRORS], *top = &g_scroll[TAB_ERRORS];
	if (*sel < *top) *top = *sel;
	if (*sel >= *top + ROWS - 1) *top = *sel - (ROWS - 2);
	for (int i = 0; i < ROWS - 1 && *top + i < g_errdict_count; i++) {
		const ErrEntry *e = &g_errdict[*top + i];
		int y = TOP + i * CHAR_H;
		if (*top + i == *sel) gfx_rect(0, y, SCREEN_W, CHAR_H, C_SEL);
		gfx_text_n(LEFT, y, e->code, strcmp(e->code, "SYMPTOM") ? C_ACCENT : C_WARN, 12);
		gfx_text_n(LEFT + 13 * CHAR_W, y, e->title, C_TEXT, COLS - 13);
	}
	gfx_text(LEFT, TOP + (ROWS - 1) * CHAR_H, "X = details. Offline dictionary - no network needed.", C_DIM);
}

static void draw_detail(void) {
	const ErrEntry *e = &g_errdict[g_detail_idx];
	int r = 0;
	gfx_text(LEFT, TOP, e->code, C_ACCENT);
	r += 1;
	r += draw_wrapped(LEFT, TOP + r * CHAR_H, e->title, C_TEXT, COLS, 2) + 1;
	gfx_text(LEFT, TOP + r * CHAR_H, "Cause", C_WARN); r++;
	r += draw_wrapped(LEFT + 20, TOP + r * CHAR_H, e->cause, C_TEXT, COLS - 2, 6) + 1;
	gfx_text(LEFT, TOP + r * CHAR_H, "What to do", C_OK); r++;
	draw_wrapped(LEFT + 20, TOP + r * CHAR_H, e->fix, C_TEXT, COLS - 2, ROWS - r);
}

/* ------------------------------------------------------------ log view --- */

static void draw_log(void) {
	char b[64];
	snprintf(b, sizeof(b), "%s  lines %d-%d / %d", BD_LOG, g_log_top + 1,
	         g_log_top + ROWS < g_log_n ? g_log_top + ROWS : g_log_n, g_log_n);
	gfx_text(LEFT, TOP - 2, b, C_ACCENT);
	for (int i = 0; i < ROWS - 1 && g_log_top + i < g_log_n; i++) {
		const char *s = g_log_lines[g_log_top + i];
		int len = strlen(s);
		const char *p = g_log_hscroll < len ? s + g_log_hscroll : "";
		uint32_t col = strstr(s, "!!!") ? C_BAD : strstr(s, "BOOT OK") ? C_OK :
		               strncmp(s, "====", 4) == 0 ? C_ACCENT : C_TEXT;
		gfx_text_n(LEFT, TOP + (i + 1) * CHAR_H - 2, p, col, COLS);
	}
}

/* ------------------------------------------------------------- dialogs --- */

static void draw_box(const char *title, const char *body) {
	int x = 60, y = 50, w = SCREEN_W - 120, h = SCREEN_H - 110;
	gfx_rect(x, y, w, h, C_PANEL);
	gfx_frame(x, y, w, h, C_ACCENT);
	gfx_text(x + 16, y + 12, title, C_ACCENT);
	draw_wrapped(x + 16, y + 44, body, C_TEXT, (w - 32) / CHAR_W, (h - 56) / CHAR_H);
}

static void draw_pickdrv(void) {
	char body[256];
	snprintf(body, sizeof(body), "More than one storage driver is enabled. Pick the one to KEEP;\n"
	         "the other driver's lines get commented out (backup first).");
	draw_box("Fix 3 - keep which driver?", body);
	for (int i = 0; i < g_pick_n; i++) {
		int y = 200 + i * (CHAR_H + 8);
		if (i == g_pick_sel) gfx_rect(80, y - 4, SCREEN_W - 160, CHAR_H + 8, C_SEL);
		gfx_text(100, y, DRV_NAME[g_pick_drv[i]], C_TEXT);
	}
	gfx_text(80, 200 + 5 * (CHAR_H + 8), "X = keep selected   O = cancel", C_DIM);
}

/* --------------------------------------------------------------- frame --- */

static void draw(void) {
	gfx_clear(C_BG);
	/* header */
	gfx_rect(0, 0, SCREEN_W, 36, C_PANEL);
	gfx_text(LEFT, 8, "VitaBootDiag " APP_VERSION, C_ACCENT);
	int x = 200;
	for (int i = 0; i < TAB_COUNT; i++) {
		int w = (strlen(TAB_NAME[i]) + 2) * CHAR_W;
		if (i == g_tab && g_view != V_LOG) gfx_rect(x, 4, w, 28, C_SEL);
		gfx_text(x + CHAR_W, 8, TAB_NAME[i], i == g_tab ? C_TEXT : C_DIM);
		x += w + 6;
	}
	if (g_view == V_LOG) gfx_text(SCREEN_W - 16 * CHAR_W, 8, "[ BOOT LOG ]", C_WARN);

	/* body */
	if (g_view == V_LOG) draw_log();
	else if (g_view == V_DETAIL) draw_detail();
	else {
		switch (g_tab) {
		case TAB_OVERVIEW: draw_overview(); break;
		case TAB_PLUGINS:  draw_plugins();  break;
		case TAB_FIXES:    draw_fixes();    break;
		case TAB_DUMPS:    draw_dumps();    break;
		case TAB_ERRORS:   draw_errors();   break;
		}
	}
	if (g_view == V_CONFIRM || g_view == V_MSG) draw_box(g_box_title, g_box_body);
	if (g_view == V_PICKDRV) draw_pickdrv();

	/* footer */
	gfx_rect(0, SCREEN_H - 30, SCREEN_W, 30, C_PANEL);
	const char *foot =
		g_view == V_LOG ? "Up/Down scroll  Left/Right pan  L/R page  O/Triangle back" :
		g_view == V_CONFIRM ? "X apply   O cancel" :
		g_view == V_MSG ? (g_msg_offer_reboot ? "X reboot now   O later" : "X / O close") :
		"L/R tab  Up/Down move  X select  O back  Triangle boot log  Square export report  START exit";
	gfx_text(LEFT, SCREEN_H - 25, foot, C_DIM);
}

/* -------------------------------------------------------------- update --- */

static int list_len(int tab) {
	switch (tab) {
	case TAB_PLUGINS: return plugin_rows();
	case TAB_FIXES:   return FIX_ITEMS;
	case TAB_DUMPS:   return g_dumps.count;
	case TAB_ERRORS:  return g_errdict_count;
	default:          return 0;
	}
}

static int update(Input in) {
	uint32_t p = in.pressed;
	if (!p) return 0;

	switch (g_view) {
	case V_CONFIRM:
		if (p & SCE_CTRL_CROSS)  { g_view = V_MAIN; run_action(g_box_action); }
		else if (p & SCE_CTRL_CIRCLE) g_view = V_MAIN;
		return 1;
	case V_MSG:
		if ((p & SCE_CTRL_CROSS) && g_msg_offer_reboot) fix5_cold_reboot();
		if (p & (SCE_CTRL_CROSS | SCE_CTRL_CIRCLE)) g_view = V_MAIN;
		return 1;
	case V_PICKDRV:
		if (p & SCE_CTRL_UP)   g_pick_sel = (g_pick_sel + g_pick_n - 1) % g_pick_n;
		if (p & SCE_CTRL_DOWN) g_pick_sel = (g_pick_sel + 1) % g_pick_n;
		if (p & SCE_CTRL_CIRCLE) g_view = V_MAIN;
		if (p & SCE_CTRL_CROSS) {
			char msg[512];
			int r = fix3_apply(&g_cfg, g_pick_drv[g_pick_sel], msg, sizeof(msg));
			rescan();
			if (r >= 0) strncat(msg, "\n\nX = reboot now   O = later", sizeof(msg) - strlen(msg) - 1);
			open_msg(r < 0 ? "Failed" : "Done", msg, r >= 0);
		}
		return 1;
	case V_LOG:
		if (p & SCE_CTRL_UP)    g_log_top--;
		if (p & SCE_CTRL_DOWN)  g_log_top++;
		if (p & SCE_CTRL_LTRIGGER) g_log_top -= ROWS - 2;
		if (p & SCE_CTRL_RTRIGGER) g_log_top += ROWS - 2;
		if (p & SCE_CTRL_LEFT)  g_log_hscroll -= 20;
		if (p & SCE_CTRL_RIGHT) g_log_hscroll += 20;
		if (g_log_top > g_log_n - (ROWS - 1)) g_log_top = g_log_n - (ROWS - 1);
		if (g_log_top < 0) g_log_top = 0;
		if (g_log_hscroll < 0) g_log_hscroll = 0;
		if (p & (SCE_CTRL_CIRCLE | SCE_CTRL_TRIANGLE)) { free_log(); g_view = V_MAIN; }
		return 1;
	case V_DETAIL:
		if (p & (SCE_CTRL_CIRCLE | SCE_CTRL_CROSS)) g_view = V_MAIN;
		if (p & SCE_CTRL_UP && g_detail_idx > 0) { g_detail_idx--; g_sel[TAB_ERRORS] = g_detail_idx; }
		if (p & SCE_CTRL_DOWN && g_detail_idx < g_errdict_count - 1) { g_detail_idx++; g_sel[TAB_ERRORS] = g_detail_idx; }
		return 1;
	default: break;
	}

	/* V_MAIN */
	if (p & SCE_CTRL_LTRIGGER) g_tab = (g_tab + TAB_COUNT - 1) % TAB_COUNT;
	if (p & SCE_CTRL_RTRIGGER) g_tab = (g_tab + 1) % TAB_COUNT;
	int n = list_len(g_tab);
	if (n > 0) {
		if (p & SCE_CTRL_UP)    g_sel[g_tab] = (g_sel[g_tab] + n - 1) % n;
		if (p & SCE_CTRL_DOWN)  g_sel[g_tab] = (g_sel[g_tab] + 1) % n;
		if (p & SCE_CTRL_LEFT)  { g_sel[g_tab] -= ROWS - 3; if (g_sel[g_tab] < 0) g_sel[g_tab] = 0; }
		if (p & SCE_CTRL_RIGHT) { g_sel[g_tab] += ROWS - 3; if (g_sel[g_tab] >= n) g_sel[g_tab] = n - 1; }
	}
	if (p & SCE_CTRL_CROSS) {
		if (g_tab == TAB_FIXES) fixes_activate(g_sel[TAB_FIXES]);
		else if (g_tab == TAB_DUMPS && g_dumps.count) fixes_activate(3);
		else if (g_tab == TAB_ERRORS) { g_detail_idx = g_sel[TAB_ERRORS]; g_view = V_DETAIL; }
	}
	if (p & SCE_CTRL_CIRCLE && g_tab != TAB_OVERVIEW) g_tab = TAB_OVERVIEW;
	if (p & SCE_CTRL_TRIANGLE) { load_log(); g_view = V_LOG; }
	if (p & SCE_CTRL_SQUARE) {
		char path[128], msg[256];
		int r = report_export(&g_dev, &g_cfg, path, sizeof(path));
		if (r >= 0) snprintf(msg, sizeof(msg), "Report written to\n%s", path);
		else        snprintf(msg, sizeof(msg), "Could not write report (0x%08X).", (unsigned)r);
		open_msg("Export report", msg, 0);
	}
	if (p & SCE_CTRL_START) return -1;
	return 1;
}

/* ---------------------------------------------------------------- main --- */

int main(void) {
	sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
	if (gfx_init() < 0) sceKernelExitProcess(0);

	gfx_clear(C_BG);
	gfx_text(LEFT, 260, "Scanning config, storage and crash dumps...", C_TEXT);
	gfx_flip();
	rescan();

	for (;;) {
		Input in = read_input();
		int r = update(in);
		if (r < 0) break;
		if (r > 0) g_dirty = 1;
		if (g_dirty) {
			draw();
			gfx_flip();          /* also waits for vblank */
			g_dirty = 0;
		} else {
			sceDisplayWaitVblankStart();
		}
	}

	cfg_free(&g_cfg);
	free_log();
	gfx_end();
	sceKernelExitProcess(0);
	return 0;
}
