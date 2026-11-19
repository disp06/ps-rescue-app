/*
 * fsutil.c - every file MODIFICATION in VitaBootDiag goes through here.
 * fs_write_allowed() is the single choke point enforcing the rule:
 *   writes / renames / deletes only on ur0: or ux0:, never vs0:/os0:/sa0:
 *   (or anything else), and never with ".." path tricks.
 */
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/io/devctl.h>
#include <psp2/rtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "fsutil.h"

int fs_write_allowed(const char *path) {
	if (!path) return 0;
	if (strncasecmp(path, "ur0:", 4) != 0 && strncasecmp(path, "ux0:", 4) != 0)
		return 0;
	if (strstr(path, "..")) return 0;
	return 1;
}

int fs_exists(const char *path) {
	SceIoStat st;
	return sceIoGetstat(path, &st) >= 0;
}

int fs_is_dir(const char *path) {
	SceIoStat st;
	if (sceIoGetstat(path, &st) < 0) return 0;
	return SCE_S_ISDIR(st.st_mode);
}

int fs_dev_mounted(const char *dev) {
	SceUID d = sceIoDopen(dev);
	if (d < 0) return 0;
	sceIoDclose(d);
	return 1;
}

int64_t fs_size(const char *path) {
	SceIoStat st;
	if (sceIoGetstat(path, &st) < 0) return -1;
	return st.st_size;
}

int fs_dev_space(const char *dev, uint64_t *free_b, uint64_t *max_b) {
	struct { uint64_t max_size, free_size; uint32_t cluster; void *unk; } di;
	memset(&di, 0, sizeof(di));
	int r = sceIoDevctl(dev, 0x3001, NULL, 0, &di, sizeof(di));
	if (r < 0) return r;
	if (free_b) *free_b = di.free_size;
	if (max_b) *max_b = di.max_size;
	return 0;
}

char *fs_read_all(const char *path, int *out_len) {
	SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
	if (fd < 0) return NULL;
	int len = (int)sceIoLseek(fd, 0, SCE_SEEK_END);
	sceIoLseek(fd, 0, SCE_SEEK_SET);
	if (len < 0 || len > 4 * 1024 * 1024) { sceIoClose(fd); return NULL; }
	char *buf = malloc(len + 1);
	if (!buf) { sceIoClose(fd); return NULL; }
	int got = 0;
	while (got < len) {
		int r = sceIoRead(fd, buf + got, len - got);
		if (r <= 0) break;
		got += r;
	}
	sceIoClose(fd);
	buf[got] = 0;
	if (out_len) *out_len = got;
	return buf;
}

int fs_write_all(const char *path, const void *data, int len) {
	if (!fs_write_allowed(path)) return FS_ERR_PROTECTED;
	SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
	if (fd < 0) return fd;
	int done = 0;
	while (done < len) {
		int r = sceIoWrite(fd, (const char *)data + done, len - done);
		if (r <= 0) { sceIoClose(fd); return r < 0 ? r : -1; }
		done += r;
	}
	sceIoClose(fd);
	return (fs_size(path) == len) ? 0 : -1;
}

int fs_append(const char *path, const char *text) {
	if (!fs_write_allowed(path)) return FS_ERR_PROTECTED;
	SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
	if (fd < 0) return fd;
	sceIoWrite(fd, text, strlen(text));
	sceIoClose(fd);
	return 0;
}

/* Replace a file "atomically enough": write .tmp, verify, swap. */
int fs_replace(const char *path, const void *data, int len) {
	if (!fs_write_allowed(path)) return FS_ERR_PROTECTED;
	char tmp[300];
	snprintf(tmp, sizeof(tmp), "%s.vbdtmp", path);
	int r = fs_write_all(tmp, data, len);
	if (r < 0) { sceIoRemove(tmp); return r; }
	sceIoRemove(path);
	r = sceIoRename(tmp, path);
	return r < 0 ? r : 0;
}

int fs_copy(const char *src, const char *dst) {
	if (!fs_write_allowed(dst)) return FS_ERR_PROTECTED;
	int len = 0;
	char *buf = fs_read_all(src, &len);
	if (!buf) return -1;
	int r = fs_write_all(dst, buf, len);
	free(buf);
	return r;
}

int fs_rename(const char *from, const char *to) {
	if (!fs_write_allowed(from) || !fs_write_allowed(to)) return FS_ERR_PROTECTED;
	return sceIoRename(from, to);
}

int fs_remove(const char *path) {
	if (!fs_write_allowed(path)) return FS_ERR_PROTECTED;
	return sceIoRemove(path);
}

int fs_mkdir(const char *path) {
	if (!fs_write_allowed(path)) return FS_ERR_PROTECTED;
	return sceIoMkdir(path, 0777);
}

/* Picks "<base>", "<base>_1", "<base>_2"... whichever does not exist yet. */
void fs_unique_name(const char *base, char *out, int outsz) {
	snprintf(out, outsz, "%s", base);
	for (int i = 1; fs_exists(out) && i < 1000; i++)
		snprintf(out, outsz, "%s_%d", base, i);
}

void fs_timestamp(char *out, int outsz) {
	SceDateTime t;
	memset(&t, 0, sizeof(t));
	sceRtcGetCurrentClockLocalTime(&t);
	snprintf(out, outsz, "%04d%02d%02d_%02d%02d%02d",
	         t.year, t.month, t.day, t.hour, t.minute, t.second);
}

const char *fs_human_size(uint64_t b, char *out, int outsz) {
	if (b >= (1ull << 30)) snprintf(out, outsz, "%.1f GB", b / 1073741824.0);
	else if (b >= (1ull << 20)) snprintf(out, outsz, "%.1f MB", b / 1048576.0);
	else if (b >= 1024) snprintf(out, outsz, "%.1f KB", b / 1024.0);
	else snprintf(out, outsz, "%u B", (unsigned)b);
	return out;
}
