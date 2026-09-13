#include "runtime.h"
int user_main(const char *args) {
    if (app_help("pulse", args))
        return 0;
    if (!strcmp(args, "quiet"))
        return 7;
    for (u32 i = 1; i <= 5; ++i) {
        print("pulse ");
        print_u32(i);
        print(" at tick ");
        print_u32(clock_ticks());
        print("\n");
        nap(150);
    }
    return 0;
}
