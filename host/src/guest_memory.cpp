#include "dh2/guest_memory.hpp"

#include <sys/mman.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace dh2 {

namespace {

bool range_valid(std::uint32_t addr, std::uint64_t len) {
    if (len == 0 || (addr & kPageMask) != 0) return false;
    return static_cast<std::uint64_t>(addr) + len <= kGuestSpaceSize;
}

std::uint8_t flags_from_prot(int prot) {
    std::uint8_t f = kPageMapped;
    if (prot & PROT_READ) f |= kPageRead;
    if (prot & PROT_WRITE) f |= kPageWrite;
    if (prot & PROT_EXEC) f |= kPageExec;
    return f;
}

}  // namespace

int host_prot(int guest_prot) {
    int p = PROT_NONE;
    if (guest_prot & (PROT_READ | PROT_EXEC)) p |= PROT_READ;
    if (guest_prot & PROT_WRITE) p |= PROT_READ | PROT_WRITE;
    return p;
}

GuestMemory::GuestMemory() : pages_(kGuestSpaceSize >> kPageShift, 0) {
    void* p = mmap(nullptr, kGuestSpaceSize + kGuardSize, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        std::fprintf(stderr, "dh2: cannot reserve the %llu MiB guest address space: %s\n",
                     static_cast<unsigned long long>((kGuestSpaceSize + kGuardSize) >> 20),
                     std::strerror(errno));
        return;
    }
    base_ = static_cast<std::uint8_t*>(p);
}

GuestMemory::~GuestMemory() {
    if (base_ != nullptr) munmap(base_, kGuestSpaceSize + kGuardSize);
}

void GuestMemory::set_flags(std::uint32_t addr, std::uint64_t len, std::uint8_t flags) {
    const std::size_t first = addr >> kPageShift;
    const std::size_t count = page_round_up(len) >> kPageShift;
    std::memset(pages_.data() + first, flags, count);
}

bool GuestMemory::map_anon(std::uint32_t addr, std::uint64_t len, int prot) {
    len = page_round_up(len);
    note_overlap("map_anon", addr, len);
    if (!ok() || !range_valid(addr, len)) return false;
    void* p = mmap(base_ + addr, len, host_prot(prot), MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (p == MAP_FAILED) return false;
    set_flags(addr, len, flags_from_prot(prot));
    return true;
}

bool GuestMemory::map_file(std::uint32_t addr, std::uint64_t len, int prot, int share_flags, int fd,
                           std::uint64_t offset) {
    len = page_round_up(len);
    note_overlap("map_file", addr, len);
    if (!ok() || !range_valid(addr, len)) return false;
    const int flags = (share_flags & (MAP_SHARED | MAP_PRIVATE)) | MAP_FIXED;
    void* p = mmap(base_ + addr, len, host_prot(prot), flags, fd, static_cast<off_t>(offset));
    if (p == MAP_FAILED) return false;
    set_flags(addr, len, flags_from_prot(prot));
    return true;
}

bool GuestMemory::copy_in(std::uint32_t addr, const void* src, std::uint64_t len) {
    if (len == 0) return true;
    std::uint8_t* dst = host_ptr(addr, len, kPageWrite);
    if (dst == nullptr) return false;
    std::memcpy(dst, src, static_cast<std::size_t>(len));
    return true;
}

bool GuestMemory::protect(std::uint32_t addr, std::uint64_t len, int prot) {
    len = page_round_up(len);
    if (!ok() || !range_valid(addr, len) || !accessible(addr, len, 0)) return false;
    if (mprotect(base_ + addr, len, host_prot(prot)) != 0) return false;
    set_flags(addr, len, flags_from_prot(prot));
    return true;
}

bool GuestMemory::unmap(std::uint32_t addr, std::uint64_t len) {
    len = page_round_up(len);
    note_overlap("unmap", addr, len);
    if (!ok() || !range_valid(addr, len)) return false;
    void* p = mmap(base_ + addr, len, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE,
                   -1, 0);
    if (p == MAP_FAILED) return false;
    set_flags(addr, len, 0);
    return true;
}

namespace {
int prot_from_flags(std::uint8_t flags) {
    int p = PROT_NONE;
    if (flags & (kPageRead | kPageExec)) p |= PROT_READ;
    if (flags & kPageWrite) p |= PROT_READ | PROT_WRITE;
    return p;
}
}  // namespace

bool GuestMemory::store32(std::uint32_t addr, std::uint32_t value) {
    if (!accessible(addr, 4, kPageRead)) return false;
    if (accessible(addr, 4, kPageWrite)) {
        std::memcpy(base_ + addr, &value, 4);
        return true;
    }
    const std::uint32_t page = page_round_down(addr);
    const std::uint8_t previous = pages_[page >> kPageShift];
    if (!protect(page, kPageSize, PROT_READ | PROT_WRITE)) return false;
    std::memcpy(base_ + addr, &value, 4);
    protect(page, kPageSize, prot_from_flags(previous));
    return true;
}

std::uint32_t GuestMemory::find_free(std::uint64_t len, std::uint32_t limit) const {
    const std::uint64_t pages = page_round_up(len) >> kPageShift;
    const std::uint64_t lowest = kLowestAlloc >> kPageShift;
    const std::uint64_t top = limit >> kPageShift;
    if (pages == 0 || top <= lowest || top - lowest < pages) return 0;
    std::uint64_t run = 0;
    for (std::uint64_t i = top; i-- > lowest;) {
        if (pages_[i] == 0) {
            if (++run == pages) return static_cast<std::uint32_t>(i << kPageShift);
        } else {
            run = 0;
        }
    }
    return 0;
}

bool GuestMemory::range_free(std::uint32_t addr, std::uint64_t len) const {
    len = page_round_up(len);
    if (!range_valid(addr, len)) return false;
    for (std::uint64_t i = addr >> kPageShift, end = (addr + len) >> kPageShift; i < end; ++i) {
        if (pages_[i] != 0) return false;
    }
    return true;
}

bool GuestMemory::accessible(std::uint32_t addr, std::uint64_t len, std::uint8_t need) const {
    if (len == 0) return true;
    const std::uint64_t end = static_cast<std::uint64_t>(addr) + len;
    if (end > kGuestSpaceSize) return false;
    const std::uint8_t mask = static_cast<std::uint8_t>(need | kPageMapped);
    for (std::uint64_t i = addr >> kPageShift, last = (end - 1) >> kPageShift; i <= last; ++i) {
        if ((pages_[i] & mask) != mask) return false;
    }
    return true;
}

void GuestMemory::note_overlap(const char* what, std::uint32_t addr, std::uint64_t len) const {
    if (watch_size_ == 0) return;
    const std::uint64_t a = addr, b = static_cast<std::uint64_t>(addr) + len;
    const std::uint64_t w = watch_base_, x = static_cast<std::uint64_t>(watch_base_) + watch_size_;
    if (a < x && w < b) {
        std::fprintf(stderr, "dh2: %s 0x%08x+0x%llx overlaps the watched range 0x%08x+0x%x\n", what,
                     addr, static_cast<unsigned long long>(len), watch_base_, watch_size_);
    }
}

std::uint8_t* GuestMemory::host_ptr(std::uint32_t addr, std::uint64_t len, std::uint8_t need) const {
    return accessible(addr, len, need) ? base_ + addr : nullptr;
}

}  // namespace dh2
