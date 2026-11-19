#define _GNU_SOURCE
/*
 * config.c - taiHEN config.txt parser + safe patcher.
 * Edits are only ever: comment a line out, insert a line, delete one of our
 * own boot_logger lines. Every save makes a timestamped backup first.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "config.h"
#include "fsutil.h"

static char *xstrdup(const char *s) {
	size_t n = strlen(s);
	char *d = malloc(n + 1);
	if (d) memcpy(d, s, n + 1);
	return d;
}

static void trim_copy(char *dst, int dsz, const char *src) {
	while (*src && isspace((unsigned char)*src)) src++;
	snprintf(dst, dsz, "%s", src);
	int n = strlen(dst);
	while (n > 0 && isspace((unsigned char)dst[n - 1])) dst[--n] = 0;
}

static int ends_with(const char *s, const char *suf) {
	int a = strlen(s), b = strlen(suf);
	return a >= b && strcasecmp(s + a - b, suf) == 0;
}

static void classify(Config *c, int i) {
	CfgLine *l = &c->lines[i];
	char t[CFG_PATH_MAX];
	trim_copy(t, sizeof(t), l->text);
	l->path[0] = 0;
	l->status = CS_NONE;
	l->warn_early_dev = 0;
	if (!t[0])            { l->type = CL_BLANK; }
	else if (t[0] == '#') { l->type = CL_COMMENT; }
	else if (t[0] == '*') { l->type = CL_SECTION; }
	else                  { l->type = CL_ENTRY; snprintf(l->path, sizeof(l->path), "%s", t); }
}

/* Recomputes section membership and plugin status for every line. */
void cfg_analyze(Config *c) {
	char section[CFG_SECTION_MAX] = "";
	for (int i = 0; i < c->count; i++) {
		CfgLine *l = &c->lines[i];
		classify(c, i);
		if (l->type == CL_SECTION) {
			char t[CFG_PATH_MAX];
			trim_copy(t, sizeof(t), l->text);
			snprintf(section, sizeof(section), "%.31s", t + 1);
		}
		snprintf(l->section, sizeof(l->section), "%s", section);
		if (l->type != CL_ENTRY) continue;

		int is_k = ends_with(l->path, ".skprx");
		int is_u = ends_with(l->path, ".suprx");
		int in_kernel = (strcasecmp(section, "KERNEL") == 0);
		char dev[16] = "";
		const char *colon = strchr(l->path, ':');
		if (colon && colon - l->path < (int)sizeof(dev) - 1) {
			memcpy(dev, l->path, colon - l->path + 1);
			dev[colon - l->path + 1] = 0;
		}

		if (!section[0])                    l->status = CS_NO_SECTION;
		else if (!colon)                    l->status = CS_BAD_PATH;
		else if (!fs_dev_mounted(dev))      l->status = CS_NODEV;
		else if (!fs_exists(l->path))       l->status = CS_MISSING;
		else if ((in_kernel && is_u) || (!in_kernel && is_k))
		                                    l->status = CS_WRONG_SECTION;
		else if (!is_k && !is_u)            l->status = CS_ODD_EXT;
		else                                l->status = CS_OK;

		/* kernel plugins are loaded before most storage drivers have
		   mounted ux0:/uma0: -> they belong in ur0:tai */
		if (in_kernel && strncasecmp(l->path, "ur0:", 4) != 0 &&
		    strncasecmp(l->path, "os0:", 4) != 0 && strncasecmp(l->path, "vs0:", 4) != 0)
			l->warn_early_dev = 1;
	}
}

static void push_line(Config *c, const char *s, int n) {
	if (c->count == c->cap) {
		c->cap = c->cap ? c->cap * 2 : 64;
		c->lines = realloc(c->lines, c->cap * sizeof(CfgLine));
	}
	CfgLine *l = &c->lines[c->count++];
	memset(l, 0, sizeof(*l));
	l->text = malloc(n + 1);
	memcpy(l->text, s, n);
	l->text[n] = 0;
}

int cfg_load(Config *c, const char *path) {
	memset(c, 0, sizeof(*c));
	snprintf(c->src, sizeof(c->src), "%s", path);
	int len = 0;
	char *buf = fs_read_all(path, &len);
	if (!buf) return -1;
	c->crlf = (strstr(buf, "\r\n") != NULL);
	c->trailing_eol = (len > 0 && buf[len - 1] == '\n');
	char *p = buf, *end = buf + len;
	while (p < end) {
		char *nl = memchr(p, '\n', end - p);
		char *le = nl ? nl : end;
		int n = le - p;
		if (n > 0 && p[n - 1] == '\r') n--;
		push_line(c, p, n);
		p = nl ? nl + 1 : end;
	}
	free(buf);
	c->loaded = 1;
	cfg_analyze(c);
	return 0;
}

void cfg_free(Config *c) {
	for (int i = 0; i < c->count; i++) free(c->lines[i].text);
	free(c->lines);
	memset(c, 0, sizeof(*c));
}

char *cfg_serialize(const Config *c, int *out_len) {
	const char *eol = c->crlf ? "\r\n" : "\n";
	int total = 1;
	for (int i = 0; i < c->count; i++) total += strlen(c->lines[i].text) + 2;
	char *buf = malloc(total);
	int n = 0;
	for (int i = 0; i < c->count; i++) {
		n += sprintf(buf + n, "%s", c->lines[i].text);
		if (i < c->count - 1 || c->trailing_eol) n += sprintf(buf + n, "%s", eol);
	}
	buf[n] = 0;
	if (out_len) *out_len = n;
	return buf;
}

int cfg_save(Config *c, char *backup_out, int bsz) {
	if (!fs_write_allowed(c->src)) return FS_ERR_PROTECTED;
	char ts[32], base[300], bak[300];
	fs_timestamp(ts, sizeof(ts));
	snprintf(base, sizeof(base), "%s.vbd_%s.bak", c->src, ts);
	fs_unique_name(base, bak, sizeof(bak));
	int r = fs_copy(c->src, bak);
	if (r < 0) return r;                       /* no backup -> no edit */
	if (backup_out) snprintf(backup_out, bsz, "%s", bak);

	int len = 0;
	char *buf = cfg_serialize(c, &len);
	r = fs_replace(c->src, buf, len);
	free(buf);
	if (r < 0) fs_copy(bak, c->src);           /* roll back */
	return r;
}

void cfg_comment_out(Config *c, int i, const char *why) {
	CfgLine *l = &c->lines[i];
	char tmp[CFG_PATH_MAX + 64];
	snprintf(tmp, sizeof(tmp), "# [VBD:%s] %s", why, l->path[0] ? l->path : l->text);
	free(l->text);
	l->text = xstrdup(tmp);
}

void cfg_insert(Config *c, int at, const char *text) {
	if (at < 0) at = 0;
	if (at > c->count) at = c->count;
	push_line(c, "", 0);                       /* grow */
	free(c->lines[c->count - 1].text);
	memmove(&c->lines[at + 1], &c->lines[at], (c->count - 1 - at) * sizeof(CfgLine));
	memset(&c->lines[at], 0, sizeof(CfgLine));
	c->lines[at].text = xstrdup(text);
	cfg_analyze(c);
}

void cfg_delete(Config *c, int i) {
	if (i < 0 || i >= c->count) return;
	free(c->lines[i].text);
	memmove(&c->lines[i], &c->lines[i + 1], (c->count - i - 1) * sizeof(CfgLine));
	c->count--;
	cfg_analyze(c);
}

int cfg_find_section(const Config *c, const char *name) {
	for (int i = 0; i < c->count; i++) {
		if (c->lines[i].type != CL_SECTION) continue;
		char t[CFG_PATH_MAX];
		trim_copy(t, sizeof(t), c->lines[i].text);
		if (strcasecmp(t + 1, name) == 0) return i;
	}
	return -1;
}

/* index after the last line belonging to the section starting at sec */
int cfg_section_end(const Config *c, int sec) {
	int last = sec;
	for (int i = sec + 1; i < c->count; i++) {
		if (c->lines[i].type == CL_SECTION) break;
		if (c->lines[i].type == CL_ENTRY || c->lines[i].type == CL_COMMENT) last = i;
	}
	return last + 1;
}

int cfg_entry_matches(const CfgLine *l, const char *needle) {
	if (l->type != CL_ENTRY) return 0;
	const char *slash = strrchr(l->path, '/');
	const char *file = slash ? slash + 1 : l->path;
	const char *colon = strrchr(file, ':');
	if (colon) file = colon + 1;
	return strcasestr(file, needle) != NULL;
}

const char *cfg_status_name(CfgStatus s) {
	switch (s) {
	case CS_OK:            return "OK";
	case CS_MISSING:       return "MISSING";
	case CS_NODEV:         return "NO DEVICE";
	case CS_WRONG_SECTION: return "WRONG SECTION";
	case CS_ODD_EXT:       return "ODD EXT";
	case CS_NO_SECTION:    return "NO SECTION";
	case CS_BAD_PATH:      return "BAD PATH";
	default:               return "";
	}
}

int cfg_is_broken(const CfgLine *l) {
	return l->type == CL_ENTRY &&
	       (l->status == CS_MISSING || l->status == CS_WRONG_SECTION ||
	        l->status == CS_BAD_PATH || l->status == CS_NO_SECTION);
}
