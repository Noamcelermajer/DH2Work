#pragma once

// Guest syscalls: the ARM EABI surface a static ARM32 Android binary actually reaches.
//
// This is deliberately a slice, not a translation layer. Each entry is written against the
// measured behaviour of the guest it serves; an unimplemented number returns -ENOSYS *and is
// named in the census*, so the next gap is always visible instead of silent.

#include <cstdint>
#include <string>
#include <vector>

#include "dh2/guest_memory.hpp"

namespace dh2 {

class Cpu;
struct LoadedImage;

struct SyscallResult {
    enum class Kind { Continue, Exit } kind = Kind::Continue;
    std::int32_t exit_code = 0;
};

class SyscallLayer {
public:
    SyscallLayer(GuestMemory& memory, Cpu& cpu, const LoadedImage& image);

    // svc is the SVC immediate; ARM Linux uses 0 with r7 as the number, and the immediate itself
    // for the private __ARM_NR_* range.
    SyscallResult handle(std::uint32_t svc);

    struct CensusEntry {
        std::int32_t number;
        std::string name;
        std::uint64_t calls;
    };
    // Every syscall that was issued, with its count. Sorted by number.
    const std::vector<CensusEntry>& census() const { return census_; }
    // Numbers that were issued but are not implemented.
    const std::vector<std::string>& unimplemented() const { return unimplemented_; }
    std::uint64_t handled() const { return handled_; }

    void print_census(FILE* out) const;

private:
    std::uint32_t& reg(std::size_t i);

    GuestMemory& mem_;
    Cpu& cpu_;
    std::uint32_t brk_base_ = 0;
    std::uint32_t brk_end_ = 0;
    std::uint32_t brk_current_ = 0;
    std::uint32_t tid_address_ = 0;
    std::uint32_t altstack_ = 0;
    std::uint64_t handled_ = 0;
    std::vector<CensusEntry> census_;
    std::vector<std::string> unimplemented_;

    std::int32_t dispatch(std::int32_t number);
};

const char* syscall_name(std::int32_t number);

}  // namespace dh2
