// exclusive.c -- LDREX/STREX must reach the exclusive monitor through our callbacks.
#include "guest_syscalls.h"

static int swap_if_equal(volatile int* word, int expected, int desired) {
    int observed;
    int status;
    do {
        __asm__ volatile("ldrex %0, [%1]" : "=r"(observed) : "r"(word) : "memory");
        if (observed != expected) {
            __asm__ volatile("clrex" ::: "memory");
            return 0;
        }
        __asm__ volatile("strex %0, %2, [%1]" : "=&r"(status) : "r"(word), "r"(desired) : "memory");
    } while (status != 0);
    return 1;
}

void dh2_main(const int* stack) {
    (void)stack;
    static volatile int word = 1;
    const int swapped = swap_if_equal(&word, 1, 2);
    const int refused = swap_if_equal(&word, 1, 3);
    dh2_write("swapped=");
    dh2_write_int(swapped);
    dh2_write(" value=");
    dh2_write_int(word);
    dh2_write(" refused=");
    dh2_write_int(!refused);
    dh2_write("\n");
    dh2_exit(0);
}
