#include "zb/guest_memory.h"

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

int failures = 0;

void expect(bool condition, const char* name) {
    if (condition) {
        std::printf("PASS %s\n", name);
    } else {
        std::printf("FAIL %s\n", name);
        ++failures;
    }
}

}  // namespace

int main() {
    zb::GuestMemory memory;
    expect(memory.ok(), "reserve 4 GiB guest space");
    expect(memory.host_page_size() == 16384, "actual 16 KiB host page size");
    if (!memory.ok() || memory.host_page_size() != 16384) return 1;

    constexpr std::uint32_t a = 0x10000;
    constexpr std::uint32_t b = a + zb::kPageSize;
    constexpr std::uint32_t c = b + zb::kPageSize;
    constexpr std::uint32_t d = c + zb::kPageSize;
    const bool mapped_a = memory.map_anon(a, zb::kPageSize, PROT_READ | PROT_WRITE);
    expect(mapped_a, "map first guest subpage");
    if (!mapped_a) return 1;
    std::memset(memory.host_ptr(a, zb::kPageSize, zb::kPageWrite), 0xA1, zb::kPageSize);
    const bool mapped_b = memory.map_anon(b, zb::kPageSize, PROT_READ | PROT_WRITE);
    expect(mapped_b, "map adjacent guest subpage");
    if (!mapped_b) return 1;
    std::memset(memory.host_ptr(b, zb::kPageSize, zb::kPageWrite), 0xB2, zb::kPageSize);
    expect(memory.base()[a] == 0xA1 && memory.base()[b] == 0xB2,
           "mapping adjacent subpage preserves live neighbor");
    expect(memory.private_anonymous(a, 2 * zb::kPageSize),
           "two private anonymous guest pages retain origin");
    expect(memory.discard_private_anonymous(b, zb::kPageSize),
           "discard private 4 KiB anonymous guest page");
    expect(memory.base()[a] == 0xA1 && memory.base()[b] == 0 &&
               memory.base()[b + zb::kPageSize - 1] == 0,
           "discard zeros target without touching 16 KiB host-page neighbor");
    std::memset(memory.host_ptr(b, zb::kPageSize, zb::kPageWrite), 0xB2, zb::kPageSize);

    expect(memory.protect(a, zb::kPageSize, PROT_READ), "protect only first guest subpage");
    expect(memory.host_ptr(a, 1, zb::kPageRead) != nullptr &&
               memory.host_ptr(a, 1, zb::kPageWrite) == nullptr &&
               memory.host_ptr(b, 1, zb::kPageWrite) != nullptr,
           "guest permissions differ inside one host page");
    expect(memory.private_anonymous(a, zb::kPageSize),
           "protect preserves private anonymous origin");
    expect(memory.unmap(b, zb::kPageSize), "unmap only second guest subpage");
    expect(memory.base()[a] == 0xA1 && memory.host_ptr(b, 1, zb::kPageRead) == nullptr,
           "unmap preserves first guest subpage");
    expect(!memory.private_anonymous(b, zb::kPageSize),
           "unmap clears private anonymous origin");
    expect(memory.map_anon(b, zb::kPageSize, PROT_READ | PROT_WRITE), "remap second guest subpage");
    expect(memory.base()[b] == 0 && memory.base()[a] == 0xA1,
           "remapped subpage is zero without clearing neighbor");

    char path[] = "/data/local/tmp/zb-16k-file-XXXXXX";
    const int fd = mkstemp(path);
    expect(fd >= 0, "create file mapping fixture");
    if (fd >= 0) {
        std::uint8_t data[3 * zb::kPageSize];
        std::memset(data, 0x11, zb::kPageSize);
        std::memset(data + zb::kPageSize, 0x22, zb::kPageSize);
        std::memset(data + 2 * zb::kPageSize, 0x33, zb::kPageSize);
        expect(write(fd, data, sizeof data) == sizeof data, "write file fixture");
        expect(memory.map_file(c, zb::kPageSize, PROT_READ, MAP_PRIVATE, fd, zb::kPageSize),
               "map private file at host-unaligned 4 KiB offset");
        expect(memory.base()[a] == 0xA1 && memory.base()[c] == 0x22,
               "file map has correct offset and preserves adjacent anonymous page");
        expect(memory.map_file(d, zb::kPageSize, PROT_READ, MAP_PRIVATE, fd, 2 * zb::kPageSize),
               "map next private file subpage");
        expect(memory.base()[c] == 0x22 && memory.base()[d] == 0x33,
               "two private file subpages coexist in host page");
        expect(memory.map_file(c, zb::kPageSize, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0),
               "replace one private file subpage");
        expect(memory.base()[c] == 0x11 && memory.base()[d] == 0x33,
               "file remap preserves neighboring file subpage");
        expect(!memory.private_anonymous(c, zb::kPageSize) &&
                   !memory.private_anonymous(b, 2 * zb::kPageSize),
               "file-backed pages are excluded from anonymous discard");
        expect(!memory.discard_private_anonymous(c, zb::kPageSize) &&
                   memory.base()[c] == 0x11 && memory.base()[d] == 0x33,
               "discard rejects file mapping and preserves bytes");
        errno = 0;
        expect(!memory.map_file(c, zb::kPageSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) &&
                   errno == ENOTSUP,
               "unsupported shared mapping fails explicitly");
        close(fd);
        unlink(path);
    }

    std::memset(memory.host_ptr(b, zb::kPageSize, zb::kPageWrite), 0xB2, zb::kPageSize);
    memory.mark_guest_backing(b, zb::kPageSize, zb::kBackingSharedAnonymous);
    expect(!memory.private_anonymous(b, zb::kPageSize),
           "shared anonymous pages are excluded from anonymous discard");
    expect(!memory.discard_private_anonymous(b, zb::kPageSize) && memory.base()[b] == 0xB2,
           "discard rejects shared anonymous mapping");
    memory.mark_guest_backing(b, zb::kPageSize, zb::kBackingUnknown);
    expect(!memory.private_anonymous(b, zb::kPageSize),
           "uncertain pages are excluded from anonymous discard");
    expect(!memory.discard_private_anonymous(b, zb::kPageSize) &&
               memory.base()[b] == 0xB2 && memory.base()[a] == 0xA1,
           "discard rejects unknown mapping and preserves neighbor");
    memory.mark_guest_backing(b, zb::kPageSize, zb::kBackingPrivateAnonymous);
    expect(memory.private_anonymous(a, 2 * zb::kPageSize),
           "private origin restored for tested anonymous pages");

    expect(memory.unmap(a, 4 * zb::kPageSize), "unmap whole host page");
    expect(memory.map_anon(a, zb::kPageSize, PROT_READ | PROT_WRITE), "remap after full host unmap");
    expect(memory.base()[a] == 0, "new host page starts zero");
    std::printf("RESULT %d failures\n", failures);
    return failures ? 1 : 0;
}
