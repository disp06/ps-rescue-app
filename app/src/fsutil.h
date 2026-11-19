#ifndef FSUTIL_H
#define FSUTIL_H
#include <stdint.h>

#define FS_ERR_PROTECTED (-0x7EAD)   /* refused by the write guard */

int      fs_write_allowed(const char *path);
int      fs_exists(const char *path);
int      fs_is_dir(const char *path);
int      fs_dev_mounted(const char *dev);
int64_t  fs_size(const char *path);
int      fs_dev_space(const char *dev, uint64_t *free_b, uint64_t *max_b);
char    *fs_read_all(const char *path, int *out_len);
int      fs_write_all(const char *path, const void *data, int len);
int      fs_append(const char *path, const char *text);
int      fs_replace(const char *path, const void *data, int len);
int      fs_copy(const char *src, const char *dst);
int      fs_rename(const char *from, const char *to);
int      fs_remove(const char *path);
int      fs_mkdir(const char *path);
void     fs_unique_name(const char *base, char *out, int outsz);
void     fs_timestamp(char *out, int outsz);
const char *fs_human_size(uint64_t b, char *out, int outsz);

#endif
