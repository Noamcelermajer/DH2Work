#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dh2/guest_memory.hpp"

namespace dh2 {

struct AuxVector {
    std::uint32_t type;
    std::uint32_t value;
};

// Build the Linux process-start stack image just below stack_top and return the initial SP.
//
// The layout is the one bionic parses out of a bare SP: argc, argv[], NULL, envp[], NULL,
// auxv pairs, AT_NULL. bionic's static __libc_init walks exactly this and derives the auxv
// pointer itself, so there is no second entry point to feed.
std::uint32_t build_initial_stack(GuestMemory& mem, std::uint32_t stack_top,
                                  const std::vector<std::string>& argv,
                                  const std::vector<std::string>& envp, std::vector<AuxVector> auxv,
                                  const std::string& execfn);

}  // namespace dh2
