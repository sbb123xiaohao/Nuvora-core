#ifndef NV_SDK_ASSERT_H
#define NV_SDK_ASSERT_H
#include <stdlib.h>
#include <stdio.h>
#ifdef NDEBUG
#define assert(x) ((void)0)
#else
#define assert(x) ((x) ? (void)0 : (puts("assertion failed: " #x), abort()))
#endif
#endif
