#include "../include/user/lib.h"

int main() {
    u_printf("Hello from user mode! PID = %d\n", sys_getpid());
    u_printf("Sleeping 5 ticks...\n");
    sys_sleep(5);
    u_printf("Awake. Exiting.\n");
    return 0;
}