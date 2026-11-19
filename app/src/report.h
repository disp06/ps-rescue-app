#ifndef REPORT_H
#define REPORT_H
#include "config.h"
#include "detect.h"
int report_export(const DeviceInfo *d, const Config *c, char *out_path, int osz);
#endif
