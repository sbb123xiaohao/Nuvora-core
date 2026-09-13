#include "runtime.h"
static volatile u32 counter;
int user_main(const char *args) {
    if (app_help("spin", args))
        return 0;
    (void)args;
    for (;;)
        ++counter;
}
