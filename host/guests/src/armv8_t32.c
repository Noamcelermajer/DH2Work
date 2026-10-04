// armv8_t32.c -- the T32 ARMv8 family our own Dynarmic patch adds.
//
// Built for Thumb-2 with -march=armv8-a. On a pristine Dynarmic every one of these raises an
// undefined-instruction exception, which is exactly what the patch is for. The values printed
// here are checked against independent host-side computations (zlib CRC-32 and the CRC-32C
// polynomial), not against this binary's own output.
#include "guest_syscalls.h"

static unsigned load_acquire_byte(const volatile unsigned char* p) {
    unsigned value;
    __asm__ volatile("ldab %0, [%1]" : "=r"(value) : "r"(p) : "memory");
    return value;
}

static unsigned load_acquire_half(const volatile unsigned short* p) {
    unsigned value;
    __asm__ volatile("ldah %0, [%1]" : "=r"(value) : "r"(p) : "memory");
    return value;
}

static unsigned load_exclusive(const volatile unsigned* p) {
    unsigned value;
    __asm__ volatile("ldaex %0, [%1]" : "=r"(value) : "r"(p) : "memory");
    return value;
}

static unsigned store_release(volatile unsigned char* p, unsigned value) {
    __asm__ volatile("stlb %0, [%1]" : : "r"(value), "r"(p) : "memory");
    return 0;
}

static unsigned store_exclusive(volatile unsigned* p, unsigned value) {
    unsigned status;
    __asm__ volatile("stlex %0, %1, [%2]" : "=&r"(status) : "r"(value), "r"(p) : "memory");
    return status;
}

static unsigned crc32_byte(unsigned crc, unsigned value) {
    __asm__ volatile("crc32b %0, %0, %1" : "+r"(crc) : "r"(value));
    return crc;
}

static unsigned crc32_half(unsigned crc, unsigned value) {
    __asm__ volatile("crc32h %0, %0, %1" : "+r"(crc) : "r"(value));
    return crc;
}

static unsigned crc32_word(unsigned crc, unsigned value) {
    __asm__ volatile("crc32w %0, %0, %1" : "+r"(crc) : "r"(value));
    return crc;
}

static unsigned crc32c_byte(unsigned crc, unsigned value) {
    __asm__ volatile("crc32cb %0, %0, %1" : "+r"(crc) : "r"(value));
    return crc;
}

static unsigned crc32c_word(unsigned crc, unsigned value) {
    __asm__ volatile("crc32cw %0, %0, %1" : "+r"(crc) : "r"(value));
    return crc;
}

void dh2_main(const int* stack) {
    (void)stack;
    static volatile unsigned char bytes[8] = {0x11, 0x22, 0, 0, 0, 0, 0, 0};
    static volatile unsigned short halves[4] = {0x3344, 0, 0, 0};
    static volatile unsigned words[2] = {0x55667788, 0};

    dh2_write("ldab=");
    dh2_write_hex32(load_acquire_byte(&bytes[1]));
    dh2_write(" ldah=");
    dh2_write_hex32(load_acquire_half(&halves[0]));
    dh2_write(" ldaex=");
    dh2_write_hex32(load_exclusive(&words[0]));

    store_release(&bytes[2], 0x7f);
    dh2_write(" stlb=");
    dh2_write_hex32(bytes[2]);
    dh2_write(" stlex=");
    dh2_write_int(store_exclusive(&words[1], 0xdeadbeef));
    dh2_write(" value=");
    dh2_write_hex32(words[1]);

    dh2_write("\ncrc32b=");
    dh2_write_hex32(crc32_byte(0, 0xff));
    dh2_write(" crc32h=");
    dh2_write_hex32(crc32_half(0, 0xffff));
    dh2_write(" crc32w=");
    dh2_write_hex32(crc32_word(0, 0xffffffff));
    dh2_write(" crc32cb=");
    dh2_write_hex32(crc32c_byte(0, 0xff));
    dh2_write(" crc32cw=");
    dh2_write_hex32(crc32c_word(0, 0xffffffff));
    dh2_write("\n");
    dh2_exit(0);
}
