#pragma once

// ARM ELF32 image loading.

#include <cstdint>
#include <string>

#include "dh2/guest_memory.hpp"

namespace dh2 {

struct LoadedImage {
    std::uint32_t bias = 0;
    std::uint32_t entry = 0;
    std::uint32_t phdr = 0;   // guest address of the program header table
    std::uint32_t phnum = 0;
    std::uint32_t phent = 0;
    std::uint32_t load_start = 0;
    std::uint32_t load_end = 0;
    bool is_dyn = false;
    std::string interp;
};

// Map an ARM ELF32 image into the guest address space.
//
// ET_EXEC is placed at its link-time addresses; ET_DYN is placed in the highest free span below
// dyn_limit (so it can grow downwards towards the linker's own allocations).
//
// Relocations: R_ARM_NONE and R_ARM_RELATIVE are applied. Any other relocation type is refused
// with a named error, because silently leaving a symbol relocation unresolved produces a fault
// far away from its cause. Dynamic symbol resolution belongs to the guest-linker work, which
// this build does not include yet.
bool load_elf32(GuestMemory& mem, const std::string& path, std::uint32_t dyn_limit, LoadedImage& out,
                std::string& error);

}  // namespace dh2
