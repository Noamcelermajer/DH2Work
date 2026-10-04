#pragma once

// The bionic-facing loader interface.
//
// The closure imports 19 slots from the platform linker through ld-android.so, which in this
// build is a trap stub: all 26 of its __loader_* exports sit on a 2-byte Thumb udf. libc.so
// reaches its own globals through __loader_shared_globals, so an unresolved (or zero-returning)
// slot is a null dereference inside libc, not a diagnosable error.
//
// Each entry is a real ARM stub in guest memory -- "svc #(0x100 + index) ; bx lr" -- so an
// imported call lands in working guest code and the host answers from the SVC callback. The
// immediate range 0x100..0x11A is disjoint from the ARM EABI numbers and from the libcall
// shim's range.

#include <cstdint>
#include <string>
#include <vector>

#include "dh2/guest_memory.hpp"

namespace dh2 {

class Cpu;

class LoaderInterface {
public:
    static constexpr std::size_t kInterfaceSlots = 26;

    LoaderInterface(GuestMemory& memory, Cpu& cpu, std::uint32_t dyn_limit);

    // Allocate the data page, the stub page and the libc_shared_globals block, and fill the
    // fields libc actually reads. auxv is the guest address of the auxv array.
    bool install(std::uint32_t argc, const std::vector<std::string>& argv, std::uint32_t auxv,
                 std::string& error);

    // The stub address for an interface symbol, or 0 when the name is not one of ours.
    std::uint32_t stub_for(const std::string& name) const;
    static bool is_interface_symbol(const std::string& name);

    // Answer an SVC in the reserved range. Returns false when the immediate is not ours.
    bool handle_svc(std::uint32_t swi);

    std::uint32_t globals() const { return globals_; }
    std::uint32_t data_page() const { return data_; }
    const std::vector<std::string>& events() const { return events_; }
    const char* const* slot_names() const;
    const std::uint64_t* slot_calls() const { return slot_calls_; }
    void print_census(FILE* out) const;

private:
    GuestMemory& mem_;
    Cpu& cpu_;
    std::uint32_t dyn_limit_;
    std::uint32_t data_ = 0;
    std::uint32_t data_used_ = 0;
    std::uint32_t stubs_ = 0;
    std::uint32_t globals_ = 0;
    std::uint32_t auxv_ = 0;
    std::uint32_t library_path_ = 0;
    std::uint32_t dlerror_text_ = 0;
    std::uint32_t dlactivity_word_ = 0;
    std::uint32_t target_sdk_ = 24;
    std::uint32_t dtors_ = 0;
    std::vector<std::string> events_;
    std::uint64_t slot_calls_[kInterfaceSlots] = {};

    std::uint32_t alloc(std::uint32_t size, std::uint32_t align);
    void write32(std::uint32_t address, std::uint32_t value);
    std::uint32_t read32(std::uint32_t address) const;
    void note(const std::string& text);
};

}  // namespace dh2
