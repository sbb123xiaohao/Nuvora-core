#ifndef NV_STRING_H
#define NV_STRING_H
#include <nv/types.h>
void *memset(void *, int, usize);
void *memcpy(void *, const void *, usize);
void *memmove(void *, const void *, usize);
int memcmp(const void *, const void *, usize);
usize strlen(const char *);
usize strnlen(const char *, usize);
int strcmp(const char *, const char *);
int strncmp(const char *, const char *, usize);
usize strlcpy(char *, const char *, usize);
int parse_u32(const char *, u32 *);
usize number(char *, u32, u32);
u32 crc32(const void *, usize);
#endif
