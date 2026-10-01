#include "runtime.h"
/* The initial process supervises the graphical session and has no shell or
 * input parser. Kernel authority is never passed to a command child. */
int user_main(const char *args) {
    (void)args;
    for (;;) {
        int pid=spawn("/apps/desktop","");
        if (pid>0) wait_task((u32)pid);
        /* A lost manager locks the account and closes its applications in
         * the kernel. The replacement desktop always presents sign-in. */
        nap(500);
    }
}
