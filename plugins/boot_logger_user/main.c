/*
 * boot_logger.suprx - VitaBootDiag shell-stage boot logger
 *
 * Put it LAST under *main in ur0:tai/config.txt so it only runs after every
 * other SceShell plugin has loaded successfully.
 *
 * module_start (runs once, inside SceShell):
 *   - marks the boot as BD_STAGE_SHELL in ur0:data/VitaBootDiag/state.bin
 *   - logs timestamp, ux0: free space and every non-firmware user module
 *     loaded into SceShell (= your *main plugins)
 * No threads, no hooks. Writes only inside ur0:data/VitaBootDiag/.
 */
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/devctl.h>
#include <psp2/rtc.h>
#include <stdarg.h>
#include "bootdiag_shared.h"

#define MAX_MODS 128

typedef struct {
	uint64_t max_size;
	uint64_t free_size;
	uint32_t cluster_size;
	void    *unk;
} DevInfo;

static char                g_buf[6144];
static int                 g_len;
static SceUID              g_mods[MAX_MODS];
static SceKernelModuleInfo g_info;

static void out(const char *fmt, ...) {
	if (g_len >= (int)sizeof(g_buf) - 1) return;
	va_list ap;
	va_start(ap, fmt);
	int n = sceClibVsnprintf(g_buf + g_len, sizeof(g_buf) - g_len, fmt, ap);
	va_end(ap);
	if (n > 0) {
		g_len += n;
		if (g_len > (int)sizeof(g_buf) - 1) g_len = sizeof(g_buf) - 1;
	}
}

static void flush_log(void) {
	SceUID fd = sceIoOpen(BD_LOG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
	if (fd < 0) return;
	sceIoWrite(fd, g_buf, g_len);   /* one write = no interleaving */
	sceIoClose(fd);
}

static int is_firmware(const char *p) {
	return sceClibStrncmp(p, "vs0:", 4) == 0 || sceClibStrncmp(p, "os0:", 4) == 0 ||
	       sceClibStrncmp(p, "app0:", 5) == 0;
}

static void mark_shell_stage(uint32_t nmods) {
	BdState s;
	sceClibMemset(&s, 0, sizeof(s));
	SceUID fd = sceIoOpen(BD_STATE, SCE_O_RDONLY, 0);
	if (fd >= 0) {
		if (sceIoRead(fd, &s, sizeof(s)) != (int)sizeof(s) || s.magic != BD_MAGIC)
			sceClibMemset(&s, 0, sizeof(s));
		sceIoClose(fd);
	}
	if (s.magic != BD_MAGIC) {      /* kernel plugin missing: still record */
		s.magic = BD_MAGIC;
		s.version = BD_VERSION;
	}
	s.stage = BD_STAGE_SHELL;
	s.fails_in_row = 0;
	s.shell_modules = nmods;
	fd = sceIoOpen(BD_STATE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd >= 0) {
		sceIoWrite(fd, &s, sizeof(s));
		sceIoClose(fd);
	}
}

void _start() __attribute__((weak, alias("module_start")));

int module_start(SceSize args, void *argp) {
	(void)args; (void)argp;
	g_len = 0;

	sceIoMkdir("ur0:data", 0777);
	sceIoMkdir(BD_DIR, 0777);

	SceDateTime t;
	sceClibMemset(&t, 0, sizeof(t));
	sceRtcGetCurrentClockLocalTime(&t);
	out("[shell] SceShell reached %04d-%02d-%02d %02d:%02d:%02d (boot_logger.suprx)\n",
	    t.year, t.month, t.day, t.hour, t.minute, t.second);

	DevInfo di;
	sceClibMemset(&di, 0, sizeof(di));
	if (sceIoDevctl("ux0:", 0x3001, NULL, 0, &di, sizeof(di)) >= 0)
		out("[shell] ux0: %u MB free of %u MB\n",
		    (unsigned)(di.free_size >> 20), (unsigned)(di.max_size >> 20));
	else
		out("[shell] ux0: NOT MOUNTED (memory card / SD2Vita driver problem?)\n");

	SceSize num = MAX_MODS;
	uint32_t total = 0;
	if (sceKernelGetModuleList(0xFF, g_mods, &num) >= 0) {
		total = num;
		out("[shell] non-firmware modules in SceShell (your *main plugins):\n");
		for (SceSize i = 0; i < num; i++) {
			sceClibMemset(&g_info, 0, sizeof(g_info));
			g_info.size = sizeof(g_info);
			if (sceKernelGetModuleInfo(g_mods[i], &g_info) < 0) continue;
			g_info.module_name[27] = 0;
			g_info.path[255] = 0;
			if (is_firmware(g_info.path)) continue;
			out("  [U] %-27s %s\n", g_info.module_name, g_info.path);
		}
		out("[shell] total SceShell modules: %u\n", (unsigned)total);
	} else {
		out("[shell] could not list SceShell modules\n");
	}
	out("[shell] BOOT OK\n");

	mark_shell_stage(total);
	flush_log();
	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
	(void)args; (void)argp;
	return SCE_KERNEL_STOP_SUCCESS;
}
