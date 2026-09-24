#ifndef NV_SDK_STDLIB_H
#define NV_SDK_STDLIB_H
#include <stddef.h>
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
_Noreturn void exit(int);
_Noreturn void abort(void);
#endif
