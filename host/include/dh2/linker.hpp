#pragma once

// The guest dynamic linker: load a shared object and its DT_NEEDED closure, resolve symbols
// between them, and apply every relocation.
//
// This is the step that lets the real game libraries load. They are ET_DYN objects with
// undefined symbols across seven dependencies -- libDungeonHunter2.so alone carries 354
// undefined functions and 54,463 relocations -- so a loader that only handles static images
// stops at the first import.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "dh2/guest_memory.hpp"
#include "dh2/loader.hpp"

namespace dh2 {

struct RelocationCensus {
    std::uint64_t relative = 0;
    std::uint64_t abs32 = 0;
    std::uint64_t glob_dat = 0;
    std::uint64_t jump_slot = 0;
    std::uint64_t none = 0;
    std::uint64_t other = 0;

    std::uint64_t total() const {
        return relative + abs32 + glob_dat + jump_slot + none + other;
    }
};

struct LinkReport {
    std::vector<std::string> load_order;      // "name  bias=0x...  [0x..., 0x...)"
    std::vector<std::string> missing_libraries;
    std::vector<std::string> unresolved;      // "module: symbol"
    std::vector<std::string> unsupported;     // "module: R_ARM_x at 0x..."
    std::vector<std::string> stubbed;         // symbols satisfied by a generated stub
    RelocationCensus relocations;
    std::uint64_t applied = 0;
    std::size_t indexed_symbols = 0;
    std::uint64_t tls_relocations = 0;
    std::uint64_t packed_relocations = 0;
    std::uint64_t static_tls_size = 0;
    std::uint64_t init_array_entries = 0;
    std::uint32_t init_array = 0;
    std::uint32_t entry = 0;

    bool ok() const { return unresolved.empty() && unsupported.empty(); }
};

class GuestLinker {
public:
    GuestLinker(GuestMemory& memory, std::uint32_t dyn_limit);

    // A directory to search when resolving a DT_NEEDED name.
    void add_search_path(const std::string& directory);
    // Satisfy a symbol no real module defines with a generated stub -- how GLES/EGL/EGL, which
    // this host does not implement yet, stop being an unresolved-import wall.
    void enable_stubs(bool on) { stubs_enabled_ = on; }

    // Resolve a name to a host-chosen guest address before any module is consulted. The loader
    // interface uses this to interpose the __loader_* entries that the platform linker would
    // otherwise define; the linker itself stays unaware of bionic.
    using Interposer = std::uint32_t (*)(void* context, const std::string& name);
    void set_interposer(Interposer interpose, void* context) {
        interposer_ = interpose;
        interposer_context_ = context;
    }

    // Load root and its dependency closure, then resolve and relocate everything.
    bool load(const std::string& root, LinkReport& report, std::string& error);

    // Absolute guest address of a defined symbol, or 0.
    std::uint32_t find_symbol(const std::string& name) const;
    const std::vector<LoadedImage>& modules() const { return modules_; }
    std::uint32_t stub_page() const { return stub_base_; }
    // Total static TLS, rounded so a thread pointer placed at its end is 16-byte aligned.
    std::uint32_t static_tls_size() const { return tls_total_; }

private:
    struct Definition {
        std::size_t module = 0;
        std::uint32_t value = 0;
        std::uint8_t info = 0;
    };

    std::string locate(const std::string& name) const;
    std::size_t load_one(const std::string& path, std::string& error);
    void index_symbols();
    std::uint32_t stub_for(const std::string& name);
    std::uint32_t resolve(std::size_t module, std::uint32_t symbol_index, LinkReport& report);
    bool relocate(LinkReport& report, std::string& error);
    bool relocate_packed(LinkReport& report, std::string& error);
    bool apply_relocation(std::size_t module, std::uint32_t type, std::uint32_t symbol_index,
                          std::uint32_t slot, LinkReport& report, std::string& error);

    GuestMemory& mem_;
    std::uint32_t dyn_limit_;
    std::vector<std::string> search_paths_;
    std::vector<LoadedImage> modules_;
    std::map<std::string, std::size_t> loaded_by_name_;
    std::map<std::string, std::vector<Definition>> index_;
    std::map<std::string, std::uint32_t> stubs_;
    // Static TLS is laid out once, below the thread pointer: module blocks are placed
    // consecutively and tls_total_ is the distance from the highest block to the thread pointer.
    std::vector<std::uint32_t> tls_offsets_;
    std::uint32_t tls_total_ = 0;
    Interposer interposer_ = nullptr;
    void* interposer_context_ = nullptr;
    std::uint32_t stub_base_ = 0;
    std::uint32_t stub_next_ = 0;
    bool stubs_enabled_ = false;
};

}  // namespace dh2
