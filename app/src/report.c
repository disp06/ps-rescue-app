/*
 * report.c - Square button: plain-text diagnostic report.
 * Goes to ux0:data/VitaBootDiag/ (falls back to ur0:data/VitaBootDiag/).
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "report.h"
#include "fsutil.h"

typedef struct { char *b; int n, cap; } Sb;

static void sb(Sb *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void sb(Sb *s, const char *fmt, ...) {
	char tmp[1024];
	va_list ap;
	va_start(ap, fmt);
	int k = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (k < 0) return;
	if (k >= (int)sizeof(tmp)) k = sizeof(tmp) - 1;
	if (s->n + k + 1 > s->cap) {
		s->cap = (s->n + k + 1) * 2;
		s->b = realloc(s->b, s->cap);
	}
	memcpy(s->b + s->n, tmp, k);
	s->n += k;
	s->b[s->n] = 0;
}

int report_export(const DeviceInfo *d, const Config *c, char *out_path, int osz) {
	Sb s = {0};
	char ts[32], h1[32], h2[32];
	fs_timestamp(ts, sizeof(ts));

	sb(&s, "VitaBootDiag report %s\n", ts);
	sb(&s, "=====================================\n\n");
	sb(&s, "Model        : %s\n", d->model);
	sb(&s, "Firmware     : %s (as reported, may be spoofed)\n", d->fw);
	sb(&s, "Mounts       : ux0:%s uma0:%s imc0:%s grw0:%s xmc0:%s\n",
	   d->ux0 ? "yes" : "NO", d->uma0 ? "yes" : "no", d->imc0 ? "yes" : "no",
	   d->grw0 ? "yes" : "no", d->xmc0 ? "yes" : "no");
	sb(&s, "ux0 space    : %s free / %s\n", fs_human_size(d->ux0_free, h1, 32), fs_human_size(d->ux0_max, h2, 32));
	sb(&s, "ur0 space    : %s free / %s\n", fs_human_size(d->ur0_free, h1, 32), fs_human_size(d->ur0_max, h2, 32));
	sb(&s, "SD2Vita      : %s\n", d->sd2vita);
	sb(&s, "storage_config.txt: %s\n", d->storage_config ? "present" : "absent");
	sb(&s, "Configs      : ur0:tai/config.txt %s, ux0:tai/config.txt %s%s\n",
	   d->ur0_cfg ? "present" : "MISSING", d->ux0_cfg ? "PRESENT" : "absent",
	   (d->ur0_cfg && d->ux0_cfg) ? "  <-- CONFLICT (Fix 1)" : "");
	if (d->drivers_active > 1) sb(&s, "!! %d storage drivers active (Fix 3)\n", d->drivers_active);
	sb(&s, "\nBoot logger  : kernel %s, shell %s\n", d->logger_k ? "installed" : "no", d->logger_u ? "installed" : "no");
	if (d->state_ok)
		sb(&s, "Boot state   : boot #%u, stage %u, previous boot stage %u, failed-in-row %u\n",
		   (unsigned)d->state.boot_count, (unsigned)d->state.stage,
		   (unsigned)d->state.prev_stage, (unsigned)d->state.fails_in_row);

	sb(&s, "\n--- ur0:tai/config.txt analysis (%d entries, %d broken) ---\n", d->entries, d->broken);
	if (c->loaded) {
		for (int i = 0; i < c->count; i++) {
			const CfgLine *l = &c->lines[i];
			if (l->type == CL_SECTION) sb(&s, "%s\n", l->text);
			else if (l->type == CL_ENTRY)
				sb(&s, "  [%-13s]%s %s\n", cfg_status_name(l->status),
				   l->warn_early_dev ? "[!early-dev]" : "", l->path);
		}
	} else sb(&s, "(could not read)\n");

	sb(&s, "\n--- crash dumps (%d, %s) ---\n", g_dumps.count, fs_human_size(g_dumps.total, h1, 32));
	for (int i = 0; i < g_dumps.count; i++)
		sb(&s, "  %s  %s\n", g_dumps.items[i].path, fs_human_size(g_dumps.items[i].size, h1, 32));

	int len = 0;
	char *log = fs_read_all(BD_LOG, &len);
	if (log) {
		const char *tail = len > 6000 ? log + len - 6000 : log;
		sb(&s, "\n--- boot.log (tail) ---\n%s\n", tail);
		free(log);
	}

	const char *dir = d->ux0 ? "ux0:data/VitaBootDiag" : "ur0:data/VitaBootDiag";
	if (d->ux0) fs_mkdir("ux0:data");
	fs_mkdir(dir);
	snprintf(out_path, osz, "%s/report_%s.txt", dir, ts);
	int r = fs_write_all(out_path, s.b, s.n);
	free(s.b);
	return r;
}
