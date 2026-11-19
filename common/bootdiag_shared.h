/*
 * bootdiag_shared.h - data shared by boot_logger.skprx, boot_logger.suprx
 * and VitaBootDiag.vpk. Keep this file dependency-free (kernel + user).
 */
#ifndef BOOTDIAG_SHARED_H
#define BOOTDIAG_SHARED_H

#include <stdint.h>

#define BD_DIR        "ur0:data/VitaBootDiag"
#define BD_LOG        BD_DIR "/boot.log"
#define BD_LOG_OLD    BD_DIR "/boot.log.old"
#define BD_STATE      BD_DIR "/state.bin"
#define BD_LOG_MAX    (48 * 1024)      /* rotate boot.log above this size */

#define BD_MAGIC      0x47444256u       /* 'VBDG' */
#define BD_VERSION    1u

/* How far a boot got. Written by the plugins, read by the app. */
enum {
	BD_STAGE_NONE   = 0,  /* never seen / state file missing           */
	BD_STAGE_KERNEL = 1,  /* boot_logger.skprx ran (*KERNEL section)    */
	BD_STAGE_SHELL  = 2   /* boot_logger.suprx ran inside SceShell      */
};

typedef struct BdState {
	uint32_t magic;
	uint32_t version;
	uint32_t boot_count;      /* incremented by the kernel plugin each boot  */
	uint32_t stage;           /* stage reached by the CURRENT/LAST boot      */
	uint32_t prev_stage;      /* stage the boot BEFORE the current one hit   */
	uint32_t fails_in_row;    /* consecutive boots that never reached shell  */
	uint32_t shell_modules;   /* user modules seen in SceShell (last boot)   */
	uint32_t kernel_modules;  /* kernel modules seen 15 s after boot         */
} BdState;

#endif
