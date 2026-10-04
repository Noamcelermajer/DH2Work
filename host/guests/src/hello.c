// hello.c -- the smallest end-to-end guest: one write and one exit.
#include "guest_syscalls.h"

void dh2_main(const int* stack) {
    (void)stack;
    dh2_write("hello arm32\n");
    dh2_exit(0);
}
