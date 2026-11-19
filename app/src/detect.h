#ifndef DETECT_H
#define DETECT_H
#include <stdint.h>
#include "config.h"
#include "bootdiag_shared.h"

#define MAX_DUMPS 128

typedef struct {
	char     path[128];
	char     name[96];
	uint64_t size;
} DumpEntry;

typedef struct {
	DumpEntry items[MAX_DUMPS];
	int       count;
	uint64_t  total;
} DumpList;

typedef struct {
	char     model[48];
	char     fw[32];
	int      pstv, has_3g;
	int      ux0, uma0, imc0, grw0, xmc0;
	uint64_t ux0_free, ux0_max, ur0_free, ur0_max;
	int      ur0_cfg, ux0_cfg, ux0_tai_dir, storage_config;
	int      drv_yamt, drv_storagemgr, drv_gamesd, drv_usbmc, drivers_active;
	char     sd2vita[96];
	int      entries, broken, nodev, early_dev;
	int      logger_k, logger_u;
	BdState  state;
	int      state_ok;
} DeviceInfo;

extern DumpList g_dumps;

void        detect_all(DeviceInfo *d, const Config *cfg);
void        detect_dumps(void);
const char *detect_boot_verdict(const DeviceInfo *d, uint32_t *col_hint);

#endif
