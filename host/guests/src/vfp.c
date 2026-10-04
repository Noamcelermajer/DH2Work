// vfp.c -- the guest must execute real VFP arithmetic, not a soft-float shim.
// The two operands are volatile so the compiler cannot fold the addition away.
#include "guest_syscalls.h"

void dh2_main(const int* stack) {
    (void)stack;
    volatile float a = 2.5f;
    volatile float b = 3.5f;
    volatile float sum = a + b;      // vadd.f32
    volatile float product = a * b;  // vmul.f32
    volatile int less = (a < b);     // vcmp.f32 / vmrs
    volatile int equal = (a == b);

    unsigned sum_bits = 0, product_bits = 0;
    __asm__ volatile("vmov %0, %1" : "=r"(sum_bits) : "t"(sum));
    __asm__ volatile("vmov %0, %1" : "=r"(product_bits) : "t"(product));

    dh2_write("sum=");
    dh2_write_hex32(sum_bits);
    dh2_write(" product=");
    dh2_write_hex32(product_bits);
    dh2_write(" less=");
    dh2_write_int(less);
    dh2_write(" equal=");
    dh2_write_int(equal);
    dh2_write("\n");
    dh2_exit(0);
}
