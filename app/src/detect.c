/*
 * detect.c - console model / 3G / storage (SD2Vita) / crash dump detection.
 * Read-only: nothing in here ever writes.
 */
#define _GNU_SOURCE
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/vshbridge.h>
#include <psp2/io/dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "detect.h"
#include "fsutil.h"

DumpList g_dumps;

static void scan_dumps_in(const char *dir) {
	SceUID d = sceIoDopen(dir);
	if (d < 0) return;
	SceIoDirent e;
	while (g_dumps.count < MAX_DUMPS) {
		memset(&e, 0, sizeof(e));
		if (sceIoDread(d, &e) <= 0) break;
		if (SCE_S_ISDIR(e.d_stat.st_mode)) continue;
		int n = strlen(e.d_name);
		if (n < 8 || strcasecmp(e.d_name + n - 8, ".psp2dmp") != 0) continue;
		DumpEntry *de = &g_dumps.items[g_dumps.count++];
		snprintf(de->path, sizeof(de->path), "%s/%s", dir, e.d_name);
		snprintf(de->name, sizeof(de->name), "%s", e.d_name);
		de->size = e.d_stat.st_size;
		g_dumps.total += de->size;
	}
	sceIoDclose(d);
}

void detect_dumps(void) {
	memset(&g_dumps, 0, sizeof(g_dumps));
	scan_dumps_in("ux0:data");
	scan_dumps_in("ur0:data");
}

static int count_active(const Config *c, const char *needle) {
	int n = 0;
	for (int i = 0; i < c->count; i++)
		if (cfg_entry_matches(&c->lines[i], needle)) n++;
	return n;
}

void detect_all(DeviceInfo *d, const Config *cfg) {
	memset(d, 0, sizeof(*d));

	/* --- model / 3G -------------------------------------------------- */
	d->pstv    = sceKernelIsPSVitaTV() ? 1 : 0;
	d->imc0    = fs_dev_mounted("imc0:");
	d->has_3g  = vshSysconHasWWAN() ? 1 : 0;
	if (d->pstv)       snprintf(d->model, sizeof(d->model), "PS TV (VTE-1000)");
	else if (d->imc0)  snprintf(d->model, sizeof(d->model), "PS Vita Slim (PCH-2000)");
	else               snprintf(d->model, sizeof(d->model), "PS Vita OLED (PCH-1000)");
	if (d->has_3g) strncat(d->model, " 3G/Wi-Fi", sizeof(d->model) - strlen(d->model) - 1);

	SceKernelSystemSwVersion v;
	memset(&v, 0, sizeof(v));
	v.size = sizeof(v);
	if (sceKernelGetSystemSwVersion(&v) >= 0)
		snprintf(d->fw, sizeof(d->fw), "%s", v.versionString);
	else
		snprintf(d->fw, sizeof(d->fw), "unknown");

	/* --- storage ----------------------------------------------------- */
	d->ux0  = fs_dev_mounted("ux0:");
	d->uma0 = fs_dev_mounted("uma0:");
	d->grw0 = fs_dev_mounted("grw0:");
	d->xmc0 = fs_dev_mounted("xmc0:");
	fs_dev_space("ux0:", &d->ux0_free, &d->ux0_max);
	fs_dev_space("ur0:", &d->ur0_free, &d->ur0_max);

	d->ur0_cfg = fs_exists("ur0:tai/config.txt");
	d->ux0_cfg = fs_exists("ux0:tai/config.txt");
	d->ux0_tai_dir = fs_is_dir("ux0:tai");
	d->storage_config = fs_exists("ur0:tai/storage_config.txt");

	if (cfg && cfg->loaded) {
		d->drv_yamt       = count_active(cfg, "yamt");
		d->drv_storagemgr = count_active(cfg, "storagemgr");
		d->drv_gamesd     = count_active(cfg, "gamesd");
		d->drv_usbmc      = count_active(cfg, "usbmc");
		d->logger_k       = count_active(cfg, "boot_logger.skprx");
		d->logger_u       = count_active(cfg, "boot_logger.suprx");
		for (int i = 0; i < cfg->count; i++) {
			const CfgLine *l = &cfg->lines[i];
			if (l->type != CL_ENTRY) continue;
			d->entries++;
			if (cfg_is_broken(l)) d->broken++;
			if (l->status == CS_NODEV) d->nodev++;
			if (l->warn_early_dev) d->early_dev++;
		}
	}
	d->drivers_active = (d->drv_yamt > 0) + (d->drv_storagemgr > 0) +
	                    (d->drv_gamesd > 0) + (d->drv_usbmc > 0);

	if (d->drivers_active == 0)
		snprintf(d->sd2vita, sizeof(d->sd2vita), "none configured (official card / internal)");
	else
		snprintf(d->sd2vita, sizeof(d->sd2vita), "%s%s%s%s-> ux0 %s",
		         d->drv_storagemgr ? "StorageMgr " : "",
		         d->drv_yamt ? "YAMT " : "",
		         d->drv_gamesd ? "gamesd " : "",
		         d->drv_usbmc ? "usbmc " : "",
		         d->ux0 ? "mounted" : "NOT MOUNTED");

	/* --- boot logger state ------------------------------------------- */
	int len = 0;
	char *s = fs_read_all("ur0:data/VitaBootDiag/state.bin", &len);
	if (s && len == (int)sizeof(BdState) && ((BdState *)s)->magic == BD_MAGIC) {
		memcpy(&d->state, s, sizeof(BdState));
		d->state_ok = 1;
	}
	free(s);

	detect_dumps();
}

const char *detect_boot_verdict(const DeviceInfo *d, uint32_t *col_hint) {
	if (!d->state_ok) { *col_hint = 0; return "No boot log yet - install boot logger (Fixes tab)"; }
	if (d->state.stage == BD_STAGE_SHELL && d->state.prev_stage != BD_STAGE_KERNEL) {
		*col_hint = 1; return "Last boot reached SceShell normally";
	}
	if (d->state.prev_stage == BD_STAGE_KERNEL) {
		*col_hint = 2;
		return "A previous boot died before SceShell - see boot log";
	}
	*col_hint = 0;
	return "Shell stage not recorded (is boot_logger.suprx in *main?)";
}
