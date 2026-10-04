// memory.c -- the guest's own memory syscalls must reach our guest address space.
#include "guest_syscalls.h"

#define DH2_PROT_READ 1
#define DH2_PROT_WRITE 2
#define DH2_MAP_PRIVATE 2
#define DH2_MAP_ANONYMOUS 0x20

void dh2_main(const int* stack) {
    (void)stack;

    unsigned char* region = (unsigned char*)dh2_syscall6(
        DH2_NR_MMAP2, 0, 65536, DH2_PROT_READ | DH2_PROT_WRITE, DH2_MAP_PRIVATE | DH2_MAP_ANONYMOUS, -1, 0);
    // The exact address depends on the allocator, so it is not part of the expectation.
    const unsigned address = (unsigned)region;
    const int mapped = address >= 0x10000u && address < 0xfffff000u;
    dh2_write("mapped=");
    dh2_write_int(mapped);

    if (mapped) {
        region[0] = 0x5a;
        region[65535] = 0xa5;
        dh2_write(" first=");
        dh2_write_hex32(region[0]);
        dh2_write(" last=");
        dh2_write_hex32(region[65535]);
        const int unmapped = dh2_syscall2(DH2_NR_MUNMAP, region, 65536);
        dh2_write(" munmap=");
        dh2_write_int(unmapped);
    }

    const int current = dh2_syscall1(DH2_NR_BRK, 0);
    const int grown = dh2_syscall1(DH2_NR_BRK, (unsigned)(current + 32768));
    dh2_write(" brk=");
    dh2_write_int(grown > current);
    dh2_write("\n");
    dh2_exit(0);
}
