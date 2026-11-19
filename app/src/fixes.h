#ifndef FIXES_H
#define FIXES_H
#include "config.h"
#include "detect.h"

enum { DRV_STORAGEMGR, DRV_YAMT, DRV_GAMESD, DRV_USBMC, DRV_COUNT };
extern const char *DRV_NAME[DRV_COUNT];

int  fix1_needed(const DeviceInfo *d);
int  fix1_apply(char *msg, int msz);
int  fix2_count(const Config *c);
int  fix2_apply(Config *c, char *msg, int msz);
int  fix3_active_mask(const Config *c);
int  fix3_apply(Config *c, int keep, char *msg, int msz);
int  fix4_apply(char *msg, int msz);
void fix5_cold_reboot(void);
int  fix5_refresh_iddat(char *msg, int msz);
int  fix5_rebuild_appdb(char *msg, int msz);
int  logger_install(Config *c, char *msg, int msz);
int  logger_uninstall(Config *c, char *msg, int msz);

#endif
