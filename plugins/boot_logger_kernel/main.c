/*
 * boot_logger.skprx - VitaBootDiag kernel-stage boot logger
 *
 * Put it FIRST under *KERNEL in ur0:tai/config.txt.
 *
 *   - module_start: bumps the boot counter, records whether the PREVIOUS boot
 *     ever reached SceShell (marker written by boot_logger.suprx), and logs
 *     the non-firmware kernel modules loaded so far.
 *   - one short-lived thread sleeps 15 s, logs which storage devices are
 *     mounted and every non-firmware kernel module (all *KERNEL plugins are
 *     in by then), then deletes itself.
 *
 * No hooks, no patches, no resident threads. Writes only inside
 * ur0:data/VitaBootDiag/.
 */
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/sysclib.h>
#include <psp2kern/kernel/sysroot.h>
#include <psp2kern/kernel/rtc.h>
#include <psp2kern/io/fcntl.h>
#include <psp2kern/io/stat.h>
#include <psp2kern/io/dirent.h>
#include <stdarg.h>
#include "bootdiag_shared.h"

#define KERNEL_PID 0x10005
#define MAX_MODS   256

static char                g_line[512];
static SceUID              g_mods[MAX_MODS];
static SceKernelModuleInfo g_info;
static BdState             g_state;

/* ---------------------------------------------------------------- io --- */

static void ensure_dir(void) {
	ksceIoMkdir("ur0:data", 0777);
	ksceIoMkdir(BD_DIR, 0777);
}

static void rotate_if_big(void) {
	SceIoStat st;
	memset(&st, 0, sizeof(st));
	if (ksceIoGetstat(BD_LOG, &st) >= 0 && st.st_size > BD_LOG_MAX) {
		ksceIoRemove(BD_LOG_OLD);
		ksceIoRename(BD_LOG, BD_LOG_OLD);
	}
}

static void log_raw(const char *s) {
	SceUID fd = ksceIoOpen(BD_LOG, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
	if (fd < 0) return;
	ksceIoWrite(fd, s, strlen(s));
	ksceIoClose(fd);
}

static void bd_logf(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(g_line, sizeof(g_line), fmt, ap);
	va_end(ap);
	log_raw(g_line);
}

static int state_read(BdState *s) {
	memset(s, 0, sizeof(*s));
	SceUID fd = ksceIoOpen(BD_STATE, SCE_O_RDONLY, 0);
	if (fd < 0) return -1;
	int r = ksceIoRead(fd, s, sizeof(*s));
	ksceIoClose(fd);
	if (r != (int)sizeof(*s) || s->magic != BD_MAGIC) {
		memset(s, 0, sizeof(*s));
		return -1;
	}
	return 0;
}

static void state_write(const BdState *s) {
	SceUID fd = ksceIoOpen(BD_STATE, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0) return;
	ksceIoWrite(fd, s, sizeof(*s));
	ksceIoClose(fd);
}

static const char *stage_name(uint32_t st) {
	switch (st) {
	case BD_STAGE_KERNEL: return "KERNEL (shell never reached)";
	case BD_STAGE_SHELL:  return "SHELL (boot completed)";
	default:              return "UNKNOWN (first run / state reset)";
	}
}

static void log_time_header(void) {
	SceDateTime t;
	memset(&t, 0, sizeof(t));
	ksceRtcGetCurrentClockLocalTime(&t);
	bd_logf("\n==== BOOT #%u  %04d-%02d-%02d %02d:%02d:%02d ====\n",
	        g_state.boot_count, t.year, t.month, t.day, t.hour, t.minute, t.second);
}

/* Logs kernel modules; 'only_custom' skips firmware modules (os0:/vs0:). */
static int log_kernel_modules(int only_custom) {
	SceSize num = MAX_MODS;
	if (ksceKernelGetModuleList(KERNEL_PID, 0x7FFFFFFF, 1, g_mods, &num) < 0)
		return -1;
	for (SceSize i = 0; i < num; i++) {
		memset(&g_info, 0, sizeof(g_info));
		g_info.size = sizeof(g_info);
		if (ksceKernelGetModuleInfo(KERNEL_PID, g_mods[i], &g_info) < 0)
			continue;
		g_info.module_name[27] = 0;
		g_info.path[255] = 0;
		int firmware = (strncmp(g_info.path, "os0:", 4) == 0 ||
		                strncmp(g_info.path, "vs0:", 4) == 0 ||
		                strncmp(g_info.path, "sd0:", 4) == 0);
		if (only_custom && firmware) continue;
		bd_logf("  [K] %-27s %s\n", g_info.module_name,
		        g_info.path[0] ? g_info.path : "(no path)");
	}
	return (int)num;
}

static const char *mount_state(const char *dev) {
	SceUID d = ksceIoDopen(dev);
	if (d < 0) return "--";
	ksceIoDclose(d);
	return "OK";
}

/* ------------------------------------------------------------ thread --- */

static int late_thread(SceSize args, void *argp) {
	(void)args; (void)argp;
	ksceKernelDelayThread(15 * 1000 * 1000);

	bd_logf("[kernel+15s] mounts: ux0:%s uma0:%s imc0:%s grw0:%s xmc0:%s\n",
	        mount_state("ux0:"), mount_state("uma0:"), mount_state("imc0:"),
	        mount_state("grw0:"), mount_state("xmc0:"));
	bd_logf("[kernel+15s] non-firmware kernel modules (taiHEN plugins etc.):\n");
	int total = log_kernel_modules(1);
	bd_logf("[kernel+15s] total kernel modules: %d\n", total);

	BdState s;
	if (state_read(&s) == 0) {
		s.kernel_modules = (total > 0) ? (uint32_t)total : 0;
		state_write(&s);
	}
	return ksceKernelExitDeleteThread(0);
}

/* ------------------------------------------------------------ module --- */

void _start() __attribute__((weak, alias("module_start")));

int module_start(SceSize args, void *argp) {
	(void)args; (void)argp;

	ensure_dir();
	rotate_if_big();

	int had_state = (state_read(&g_state) == 0);
	uint32_t prev = had_state ? g_state.stage : BD_STAGE_NONE;

	g_state.magic      = BD_MAGIC;
	g_state.version    = BD_VERSION;
	g_state.boot_count = had_state ? g_state.boot_count + 1 : 1;
	g_state.prev_stage = prev;
	if (had_state && prev != BD_STAGE_SHELL) g_state.fails_in_row++;
	else                                      g_state.fails_in_row = 0;
	g_state.stage = BD_STAGE_KERNEL;
	state_write(&g_state);

	log_time_header();
	bd_logf("[kernel] boot_logger.skprx started (fw 0x%08X)\n",
	        (unsigned)ksceKernelSysrootGetSystemSwVersion());
	bd_logf("[kernel] previous boot reached: %s\n", stage_name(prev));
	if (had_state && prev == BD_STAGE_KERNEL)
		bd_logf("[kernel] !!! PREVIOUS BOOT DIED BETWEEN KERNEL STAGE AND SCESHELL"
		        " (%u in a row). Suspect a *KERNEL plugin listed after this one,"
		        " or a *main plugin listed before boot_logger.suprx\n",
		        (unsigned)g_state.fails_in_row);
	bd_logf("[kernel] non-firmware modules loaded before this plugin:\n");
	log_kernel_modules(1);

	SceUID th = ksceKernelCreateThread("bootdiag_late", late_thread,
	                                   0x3C, 0x2000, 0, 0, NULL);
	if (th >= 0) ksceKernelStartThread(th, 0, NULL);

	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
	(void)args; (void)argp;
	return SCE_KERNEL_STOP_SUCCESS;
}
