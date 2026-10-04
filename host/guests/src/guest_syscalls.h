// Minimal raw-syscall support for the freestanding guest test programs.
//
// These guests deliberately link no C library: they exercise the loader, the JIT and the syscall
// layer directly, so a failure names the layer instead of naming bionic.
#ifndef DH2_GUEST_SYSCALLS_H
#define DH2_GUEST_SYSCALLS_H

// Implemented in start.S. The Linux ARM EABI takes the syscall number in r7, and in Thumb state
// clang reserves r7 as the frame pointer, so the trap is written in assembly rather than as a
// pinned-register inline asm block.
extern int dh2_svc_asm(int number, int a0, int a1, int a2, int a3, int a4, int a5);

static inline int dh2_svc(int n, int a0, int a1, int a2, int a3, int a4, int a5) {
    return dh2_svc_asm(n, a0, a1, a2, a3, a4, a5);
}

#define dh2_syscall0(n) dh2_svc((n), 0, 0, 0, 0, 0, 0)
#define dh2_syscall1(n, a) dh2_svc((n), (int)(a), 0, 0, 0, 0, 0)
#define dh2_syscall2(n, a, b) dh2_svc((n), (int)(a), (int)(b), 0, 0, 0, 0)
#define dh2_syscall3(n, a, b, c) dh2_svc((n), (int)(a), (int)(b), (int)(c), 0, 0, 0)
#define dh2_syscall6(n, a, b, c, d, e, f) \
    dh2_svc((n), (int)(a), (int)(b), (int)(c), (int)(d), (int)(e), (int)(f))

#define DH2_NR_WRITE 4
#define DH2_NR_EXIT_GROUP 248
#define DH2_NR_MMAP2 192
#define DH2_NR_MUNMAP 91
#define DH2_NR_BRK 45

static inline unsigned dh2_strlen(const char* s) {
    unsigned n = 0;
    while (s[n] != 0) ++n;
    return n;
}

static inline void dh2_write(const char* s) {
    dh2_syscall3(DH2_NR_WRITE, 1, s, (int)dh2_strlen(s));
}

static inline void dh2_write_uint(unsigned value) {
    char digits[12];
    int count = 0;
    if (value == 0) {
        dh2_write("0");
        return;
    }
    while (value != 0) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    char out[12];
    int j = 0;
    while (count != 0) out[j++] = digits[--count];
    out[j] = 0;
    dh2_write(out);
}

static inline void dh2_write_int(int value) {
    if (value < 0) {
        dh2_write("-");
        dh2_write_uint((unsigned)(-value));
    } else {
        dh2_write_uint((unsigned)value);
    }
}

static inline void dh2_write_hex32(unsigned value) {
    static const char* kDigits = "0123456789abcdef";
    char out[11];
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 8; ++i) out[2 + i] = kDigits[(value >> ((7 - i) * 4)) & 0xf];
    out[10] = 0;
    dh2_write(out);
}

static inline void dh2_exit(int code) {
    dh2_syscall1(DH2_NR_EXIT_GROUP, code);
    for (;;) {
    }
}

#endif
