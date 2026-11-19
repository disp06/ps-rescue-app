#ifndef ERRDICT_H
#define ERRDICT_H
#include <stdint.h>

typedef struct {
	const char *code;
	const char *title;
	const char *cause;
	const char *fix;
} ErrEntry;

extern const ErrEntry g_errdict[];
extern const int      g_errdict_count;

const ErrEntry *errdict_find(const char *code);
void            errdict_decode_hex(uint32_t code, char *out, int outsz);

#endif
