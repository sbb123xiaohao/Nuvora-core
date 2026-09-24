#ifndef NV_SDK_STDIO_H
#define NV_SDK_STDIO_H
int puts(const char *);
int putchar(int);
/* Integers/strings only: %s %c %d %i %u %x %% and l/ll integer lengths. */
int printf(const char *, ...);
#endif
