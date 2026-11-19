/*
 * fixes.c - the repair actions. Every one of them:
 *   - only touches ur0:/ux0: (enforced again inside fsutil),
 *   - renames instead of deleting where the data could matter,
 *   - backs up config.txt before changing it.
 */
#include <psp2/power.h>
#include <stdio.h>
#include <string.h>
#include "fixes.h"
#include "fsutil.h"

#define CFG_PATH "ur0:tai/config.txt"

static const char *DRV_NEEDLE[DRV_COUNT] = { "storagemgr", "yamt", "gamesd", "usbmc" };
const char *DRV_NAME[DRV_COUNT] = { "StorageMgr", "YAMT", "gamesd", "usbmc" };

/* ---------------------------------------------------------- Fix 1 --- */
int fix1_needed(const DeviceInfo *d) {
	return d->ur0_cfg && d->ux0_cfg;
}

int fix1_apply(char *msg, int msz) {
	if (!fs_exists("ur0:tai/config.txt")) {
		snprintf(msg, msz, "ur0:tai/config.txt missing - refusing (it would leave you with no config).");
		return -1;
	}
	char dst[64];
	fs_unique_name("ux0:tai_backup", dst, sizeof(dst));
	int r = fs_rename("ux0:tai", dst);
	if (r < 0) snprintf(msg, msz, "Rename failed (0x%08X).", (unsigned)r);
	else       snprintf(msg, msz, "ux0:tai renamed to %s. taiHEN now uses ur0:tai/config.txt only.", dst);
	return r;
}

/* ---------------------------------------------------------- Fix 2 --- */
int fix2_count(const Config *c) {
	int n = 0;
	for (int i = 0; i < c->count; i++) if (cfg_is_broken(&c->lines[i])) n++;
	return n;
}

int fix2_apply(Config *c, char *msg, int msz) {
	int n = 0;
	for (int i = 0; i < c->count; i++) {
		if (!cfg_is_broken(&c->lines[i])) continue;
		const char *why = c->lines[i].status == CS_MISSING ? "missing" :
		                  c->lines[i].status == CS_WRONG_SECTION ? "wrong-section" : "invalid";
		cfg_comment_out(c, i, why);
		n++;
	}
	if (!n) { snprintf(msg, msz, "Nothing to do."); return 0; }
	char bak[300];
	int r = cfg_save(c, bak, sizeof(bak));
	if (r < 0) snprintf(msg, msz, "Save failed (0x%08X). config.txt unchanged.", (unsigned)r);
	else       snprintf(msg, msz, "Commented out %d line(s). Backup: %s", n, bak);
	return r;
}

/* ---------------------------------------------------------- Fix 3 --- */
int fix3_active_mask(const Config *c) {
	int mask = 0;
	for (int k = 0; k < DRV_COUNT; k++)
		for (int i = 0; i < c->count; i++)
			if (cfg_entry_matches(&c->lines[i], DRV_NEEDLE[k])) { mask |= 1 << k; break; }
	return mask;
}

int fix3_apply(Config *c, int keep, char *msg, int msz) {
	int n = 0;
	for (int i = 0; i < c->count; i++) {
		for (int k = 0; k < DRV_COUNT; k++) {
			if (k == keep) continue;
			if (cfg_entry_matches(&c->lines[i], DRV_NEEDLE[k])) {
				cfg_comment_out(c, i, "dup-storage-driver");
				n++;
				break;
			}
		}
	}
	if (!n) { snprintf(msg, msz, "No other storage driver lines found."); return 0; }
	char bak[300];
	int r = cfg_save(c, bak, sizeof(bak));
	if (r < 0) snprintf(msg, msz, "Save failed (0x%08X). config.txt unchanged.", (unsigned)r);
	else       snprintf(msg, msz, "Kept %s, disabled %d line(s). Backup: %s. Reboot to apply.",
	                    DRV_NAME[keep], n, bak);
	return r;
}

/* ---------------------------------------------------------- Fix 4 --- */
int fix4_apply(char *msg, int msz) {
	int ok = 0, fail = 0;
	uint64_t freed = 0;
	for (int i = 0; i < g_dumps.count; i++) {
		if (fs_remove(g_dumps.items[i].path) >= 0) { ok++; freed += g_dumps.items[i].size; }
		else fail++;
	}
	char hs[32];
	snprintf(msg, msz, "Deleted %d dump(s), freed %s.%s", ok, fs_human_size(freed, hs, sizeof(hs)),
	         fail ? " Some could not be deleted." : "");
	detect_dumps();
	return fail ? -1 : 0;
}

/* ---------------------------------------------------------- Fix 5 --- */
void fix5_cold_reboot(void) {
	scePowerRequestColdReset();
}

int fix5_refresh_iddat(char *msg, int msz) {
	if (!fs_exists("ux0:id.dat")) { snprintf(msg, msz, "ux0:id.dat not found (already removed?)."); return -1; }
	char ts[32], base[64], dst[64];
	fs_timestamp(ts, sizeof(ts));
	snprintf(base, sizeof(base), "ux0:id.dat.vbd_%s.bak", ts);
	fs_unique_name(base, dst, sizeof(dst));
	int r = fs_rename("ux0:id.dat", dst);
	if (r < 0) snprintf(msg, msz, "Rename failed (0x%08X).", (unsigned)r);
	else       snprintf(msg, msz, "ux0:id.dat -> %s. The card is re-indexed on next boot.", dst);
	return r;
}

int fix5_rebuild_appdb(char *msg, int msz) {
	const char *db = "ur0:shell/db/app.db";
	if (!fs_exists(db)) { snprintf(msg, msz, "%s not found.", db); return -1; }
	char ts[32], base[64], dst[64];
	fs_timestamp(ts, sizeof(ts));
	snprintf(base, sizeof(base), "ur0:shell/db/app.db.vbd_%s.bak", ts);
	fs_unique_name(base, dst, sizeof(dst));
	int r = fs_rename(db, dst);
	if (r < 0) snprintf(msg, msz, "Rename failed (0x%08X).", (unsigned)r);
	else       snprintf(msg, msz, "app.db moved to %s. The database is rebuilt on next boot.", dst);
	return r;
}

/* ------------------------------------------------- boot logger install --- */
int logger_install(Config *c, char *msg, int msz) {
	if (!c->loaded) { snprintf(msg, msz, "ur0:tai/config.txt not loaded - cannot install."); return -1; }
	int r1 = fs_copy("app0:plugins/boot_logger.skprx", "ur0:tai/boot_logger.skprx");
	int r2 = fs_copy("app0:plugins/boot_logger.suprx", "ur0:tai/boot_logger.suprx");
	if (r1 < 0 || r2 < 0) {
		snprintf(msg, msz, "Copy to ur0:tai failed (0x%08X / 0x%08X).", (unsigned)r1, (unsigned)r2);
		return -1;
	}
	int changed = 0, has_k = 0, has_u = 0;
	for (int i = 0; i < c->count; i++) {
		if (cfg_entry_matches(&c->lines[i], "boot_logger.skprx")) has_k = 1;
		if (cfg_entry_matches(&c->lines[i], "boot_logger.suprx")) has_u = 1;
	}
	if (!has_k) {                         /* FIRST in *KERNEL */
		int sec = cfg_find_section(c, "KERNEL");
		if (sec < 0) { cfg_insert(c, 0, "*KERNEL"); sec = 0; }
		cfg_insert(c, sec + 1, "ur0:tai/boot_logger.skprx");
		changed++;
	}
	if (!has_u) {                         /* LAST in *main */
		int sec = cfg_find_section(c, "main");
		if (sec < 0) {
			cfg_insert(c, c->count, "*main");
			sec = c->count - 1;
		}
		cfg_insert(c, cfg_section_end(c, sec), "ur0:tai/boot_logger.suprx");
		changed++;
	}
	if (!changed) { snprintf(msg, msz, "Plugins updated in ur0:tai. config.txt already had both lines."); return 0; }
	char bak[300];
	int r = cfg_save(c, bak, sizeof(bak));
	if (r < 0) snprintf(msg, msz, "Plugins copied, but config.txt save failed (0x%08X).", (unsigned)r);
	else       snprintf(msg, msz, "Installed. Backup: %s. Reboot to start logging.", bak);
	return r;
}

int logger_uninstall(Config *c, char *msg, int msz) {
	int n = 0;
	for (int i = c->count - 1; i >= 0; i--) {
		if (cfg_entry_matches(&c->lines[i], "boot_logger.skprx") ||
		    cfg_entry_matches(&c->lines[i], "boot_logger.suprx")) {
			cfg_delete(c, i);
			n++;
		}
	}
	int r = 0;
	char bak[300] = "";
	if (n) r = cfg_save(c, bak, sizeof(bak));
	if (r >= 0) {
		fs_remove("ur0:tai/boot_logger.skprx");
		fs_remove("ur0:tai/boot_logger.suprx");
		snprintf(msg, msz, "Removed %d line(s) and the plugin files.%s%s", n,
		         n ? " Backup: " : "", bak);
	} else {
		snprintf(msg, msz, "Save failed (0x%08X). Nothing removed.", (unsigned)r);
	}
	return r;
}
