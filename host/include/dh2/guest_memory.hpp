#pragma once

// Guest address space.
//
// The guest sees one flat 4 GiB ARM32 address space. We reserve it once as PROT_NONE and then
// hand out fixed sub-ranges with MAP_FIXED. Two consequences matter:
//
//   * the reservation is not host-writable, so a guest wild pointer cannot silently corrupt the
//     host process -- it is refused by the page-flag check in accessible() and reported as a
//     guest fault by the CPU callbacks;
//   * nothing is ever mapped at address 0, so a guest null dereference faults instead of
//     reading host memory.
//
// guest_page_size() is deliberately fixed at 4096 here. The shipped launcher on this project
// refuses hosts whose page size is not 4096; the 16 KiB port is a separate, unfinished track.

#include <cstdint>
#include <vector>

namespace dh2 {

inline constexpr std::uint64_t kGuestSpaceSize = 1ull << 32;
inline constexpr std::uint32_t kPageSize = 4096;
inline constexpr std::uint32_t kPageShift = 12;
inline constexpr std::uint32_t kPageMask = kPageSize - 1;

// The lowest address any mapping may use; keeps the zero page unmapped.
inline constexpr std::uint32_t kLowestAlloc = 0x10000;
// A PROT_NONE tail after the 4 GiB so a guest address just past the top cannot reach host memory.
inline constexpr std::uint64_t kGuardSize = 64 * 1024;

enum PageFlag : std::uint8_t {
    kPageMapped = 0x1,
    kPageRead = 0x2,
    kPageWrite = 0x4,
    kPageExec = 0x8,
};

constexpr std::uint64_t page_round_up(std::uint64_t v) {
    return (v + kPageMask) & ~static_cast<std::uint64_t>(kPageMask);
}
constexpr std::uint64_t page_round_down(std::uint64_t v) {
    return v & ~static_cast<std::uint64_t>(kPageMask);
}

// Guest PROT_* -> host PROT_*. Guest executable pages become host-readable-only: the JIT reads
// guest instructions through the memory callbacks, so the host never needs to execute them
// (keeps the host W^X-clean) but must still be able to read them.
int host_prot(int guest_prot);

class GuestMemory {
public:
    GuestMemory();
    ~GuestMemory();
    GuestMemory(const GuestMemory&) = delete;
    GuestMemory& operator=(const GuestMemory&) = delete;

    bool ok() const { return base_ != nullptr; }
    std::uint8_t* base() const { return base_; }

    // Map len bytes at addr. addr and len must be page-aligned (len is rounded up).
    bool map_anon(std::uint32_t addr, std::uint64_t len, int prot);
    bool map_file(std::uint32_t addr, std::uint64_t len, int prot, int share_flags, int fd,
                  std::uint64_t offset);
    // Write bytes into an already-mapped region (used to materialise PT_LOAD segment contents).
    bool copy_in(std::uint32_t addr, const void* src, std::uint64_t len);
    bool protect(std::uint32_t addr, std::uint64_t len, int prot);
    bool unmap(std::uint32_t addr, std::uint64_t len);

    // Every page in [addr, addr+len) carries at least the flags in need (OR kPageMapped).
    bool accessible(std::uint32_t addr, std::uint64_t len, std::uint8_t need) const;
    std::uint8_t* host_ptr(std::uint32_t addr, std::uint64_t len, std::uint8_t need) const;

    // The lowest address >= kLowestAlloc of a free run of len bytes below limit, or 0.
    std::uint32_t find_free(std::uint64_t len, std::uint32_t limit) const;
    bool range_free(std::uint32_t addr, std::uint64_t len) const;

    // Page flags, one byte per 4 KiB page. Used by the self test and diagnostics.
    const std::vector<std::uint8_t>& page_flags() const { return pages_; }

private:
    void set_flags(std::uint32_t addr, std::uint64_t len, std::uint8_t flags);

    std::uint8_t* base_ = nullptr;
    std::vector<std::uint8_t> pages_;
};

}  // namespace dh2
