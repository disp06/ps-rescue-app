/*
 * errdict.c - offline error / symptom dictionary.
 * Entries are kept to things that are well documented in the homebrew scene
 * (SCE errno codes, taiHEN return codes, common boot symptoms). Unknown hex
 * codes still get a facility-level decode.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "errdict.h"

const ErrEntry g_errdict[] = {
	/* ---------------------------------------------- symptoms ------ */
	{ "SYMPTOM", "Boot loop / black screen after adding a plugin",
	  "A plugin in ur0:tai/config.txt crashes the kernel or SceShell during boot.",
	  "On Enso, power on while holding SELECT: optional ('-') entries in "
	  "ur0:tai/boot_config.txt are skipped, so taiHEN plugins are normally not loaded. "
	  "Then run this app: Fix 2 disables missing/broken entries, the boot log shows "
	  "the last stage reached. A timestamped config backup is made before every edit." },
	{ "SYMPTOM", "Edits to ur0:tai/config.txt are ignored",
	  "A second config exists in ux0:tai/. taiHEN prefers ux0:tai/config.txt when it is present.",
	  "Fix 1 renames ux0:tai to ux0:tai_backup so only ur0:tai/config.txt is used. "
	  "Copy any plugin lines you still need from the backup into ur0:tai/config.txt." },
	{ "SYMPTOM", "'Insert a memory card' / games vanished after SD2Vita",
	  "The SD2Vita driver did not mount ux0: (driver missing, wrong section, two drivers fighting, "
	  "or the microSD is not exFAT/FAT32).",
	  "Check the Overview tab: ux0 mount state and active drivers. Use only one of "
	  "StorageMgr / YAMT / gamesd (Fix 3). Drivers must live in ur0:tai and be under *KERNEL." },
	{ "SYMPTOM", "Both YAMT and StorageMgr enabled",
	  "Two storage drivers try to remount ux0:/uma0:. Results range from wrong card mounted to boot hangs.",
	  "Fix 3 keeps the driver you choose and comments out the other's lines (backup made first)." },
	{ "SYMPTOM", "Kernel plugin path on ux0:/uma0:",
	  "*KERNEL plugins are loaded early; with an SD2Vita driver ux0: may not exist yet, so the plugin "
	  "silently fails to load (or a storage driver itself can never load).",
	  "Move .skprx files to ur0:tai/ and point the *KERNEL lines there." },
	{ "SYMPTOM", "LiveArea bubbles show '?' / apps missing",
	  "The LiveArea app database (ur0:shell/db/app.db) does not match what is installed.",
	  "Try VitaShell 'Refresh LiveArea' first. Fix 5 can rename ux0:id.dat (soft re-index) or "
	  "app.db (full rebuild - loses folders/layout) and then cold-reboot." },
	{ "SYMPTOM", "Previous boot died before SceShell (boot log)",
	  "boot_logger.skprx ran but boot_logger.suprx never did, so something between the kernel stage "
	  "and the end of *main crashed.",
	  "Look at the [K] lines of the failed boot: plugins listed after boot_logger.skprx and any *main "
	  "plugin above boot_logger.suprx are the suspects. Disable them one at a time." },
	{ "C2-12828-1", "An error has occurred in the application",
	  "The running app crashed (bad homebrew build, incompatible plugin hooking it, corrupted data).",
	  "A .psp2dmp crash dump is usually written to ux0:data. Check the Dumps tab for the module name, "
	  "reinstall the app and disable plugins that hook that title. Fix 4 deletes old dumps." },

	/* ---------------------------------------------- taiHEN -------- */
	{ "0x90010000", "TAI_ERROR_SYSTEM",  "Generic taiHEN failure.", "Reinstall HENkaku/Enso; check the plugin is for your firmware." },
	{ "0x90010001", "TAI_ERROR_MEMORY",  "taiHEN ran out of memory while loading/patching.", "Too many or too large plugins. Disable some." },
	{ "0x90010002", "TAI_ERROR_NOT_FOUND", "The function/module a plugin wanted to hook was not found.", "Plugin made for another firmware or depends on a module that is not loaded." },
	{ "0x90010003", "TAI_ERROR_INVALID_ARGS", "Bad arguments passed to taiHEN.", "Plugin bug. Update the plugin." },
	{ "0x90010004", "TAI_ERROR_INVALID_KERNEL_ADDR", "A kernel address was invalid.", "Plugin bug or wrong firmware offsets." },
	{ "0x90010005", "TAI_ERROR_PATCH_EXISTS", "Another plugin already patched the same spot.", "Two plugins doing the same job. Keep one." },
	{ "0x90010006", "TAI_ERROR_HOOK_ERROR", "Hook could not be installed.", "Plugin conflict or wrong firmware." },
	{ "0x90010007", "TAI_ERROR_NOT_IMPLEMENTED", "Feature not implemented in this taiHEN.", "Update taiHEN / HENkaku." },
	{ "0x90010008", "TAI_ERROR_USER_MEMORY", "Problem accessing user memory.", "Plugin bug." },
	{ "0x90010009", "TAI_ERROR_NOT_ALLOWED", "Operation not allowed.", "Plugin loaded in the wrong section or process." },
	{ "0x9001000A", "TAI_ERROR_STUB_NOT_RESOLVED", "An import of the plugin could not be resolved.", "Missing dependency plugin or firmware mismatch." },
	{ "0x9001000B", "TAI_ERROR_INVALID_MODULE", "The module file is not valid.", "Corrupt/partial copy. Re-copy the .skprx/.suprx." },
	{ "0x9001000C", "TAI_ERROR_MODULE_OVERFLOW", "Too many modules/patches.", "Disable some plugins." },
	{ "0x9001000D", "TAI_ERROR_BLOCKING", "Operation would block.", "Usually transient; retry." },

	/* ---------------------------------------------- SCE errno ----- */
	{ "0x80010002", "ENOENT - no such file or directory", "A path does not exist.", "Check the path in config.txt / app data. Fix 2 handles missing plugins." },
	{ "0x80010005", "EIO - input/output error", "Read/write failed at the device level.", "Card or SD2Vita adapter problem. Back up and check/format the card." },
	{ "0x80010009", "EBADF - bad file descriptor", "Program used a closed/invalid file handle.", "App bug." },
	{ "0x8001000C", "ENOMEM - out of memory", "Allocation failed.", "Close other apps; too many plugins in the process." },
	{ "0x8001000D", "EACCES - permission denied", "Access to the path is not allowed for this app.", "Safe homebrew cannot write ur0:/vs0:. Use an unsafe build." },
	{ "0x80010011", "EEXIST - already exists", "Target file/folder already exists.", "Rename or remove the existing item." },
	{ "0x80010013", "ENODEV - no such device", "The device (ux0:, uma0:...) is not mounted.", "Storage driver did not mount it. See SD2Vita symptoms." },
	{ "0x80010014", "ENOTDIR - not a directory", "A path component is a file.", "Fix the path." },
	{ "0x80010015", "EISDIR - is a directory", "Expected a file, got a folder.", "Fix the path." },
	{ "0x80010016", "EINVAL - invalid argument", "Bad parameter.", "App bug." },
	{ "0x8001001C", "ENOSPC - no space left", "Device is full.", "Free space; Fix 4 removes crash dumps." },
	{ "0x8001001E", "EROFS - read-only file system", "Device mounted read-only.", "Normal for vs0:/os0:. For ux0 check the card's lock/health." },
};
const int g_errdict_count = sizeof(g_errdict) / sizeof(g_errdict[0]);

const ErrEntry *errdict_find(const char *code) {
	for (int i = 0; i < g_errdict_count; i++)
		if (strcasecmp(g_errdict[i].code, code) == 0) return &g_errdict[i];
	return NULL;
}

/* Facility-level decode for codes that are not in the table. */
void errdict_decode_hex(uint32_t code, char *out, int outsz) {
	char key[16];
	snprintf(key, sizeof(key), "0x%08X", (unsigned)code);
	const ErrEntry *e = errdict_find(key);
	if (e) { snprintf(out, outsz, "%s: %s", key, e->title); return; }
	uint32_t fac = (code >> 16) & 0x0FFF;
	const char *fname =
		(code & 0x80000000u) == 0 ? "not an error code" :
		(code >> 16) == 0x9001 ? "taiHEN" :
		fac == 0x001 ? "SCE errno" :
		fac == 0x002 ? "SCE kernel (sysmem/modulemgr/threadmgr)" :
		"unknown facility";
	snprintf(out, outsz, "%s: facility 0x%03X (%s), sub-code 0x%04X",
	         key, (unsigned)fac, fname, (unsigned)(code & 0xFFFF));
}
