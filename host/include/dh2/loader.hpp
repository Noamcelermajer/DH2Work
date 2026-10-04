#pragma once

// ARM ELF32 image loading.

#include <cstdint>
#include <string>
#include <vector>

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

    // Dynamic-table contents. Every address below is a *link-time* value: add bias to address
    // guest memory. map_elf() fills these; load_elf32() only needs the relocation ones.
    std::string soname;
    std::vector<std::string> needed;
    std::uint32_t dynamic_addr = 0;  // guest address of PT_DYNAMIC (already biased)
    std::uint32_t dynamic_size = 0;
    bool textrel = false;
    std::uint32_t dt_strtab = 0, dt_strsz = 0;
    std::uint32_t dt_symtab = 0, dt_syment = 16, dt_nsyms = 0;
    std::uint32_t dt_hash = 0, dt_gnu_hash = 0;
    std::uint32_t dt_rel = 0, dt_relsz = 0, dt_relent = 8;
    std::uint32_t dt_jmprel = 0, dt_pltrelsz = 0;
    // Android packs relocations to save space. DT_ANDROID_REL is an APS2-encoded stream of
    // (offset, r_info) groups; RELR is a word-per-address bitmap of relative relocations. A
    // library can carry only these and no DT_REL at all.
    std::uint32_t dt_android_rel = 0, dt_android_relsz = 0;
    std::uint32_t dt_android_relr = 0, dt_android_relrsz = 0, dt_android_relrent = 4;
    std::uint32_t dt_relr = 0, dt_relrsz = 0, dt_relrent = 4;
    // PT_TLS: the module's static TLS template. The linker lays these out below the thread
    // pointer, which is what R_ARM_TLS_TPOFF32 is expressed against.
    std::uint32_t tls_vaddr = 0, tls_filesz = 0, tls_memsz = 0, tls_align = 1;

    std::uint32_t dt_init = 0, dt_fini = 0;
    std::uint32_t dt_init_array = 0, dt_init_arraysz = 0;
    std::uint32_t dt_fini_array = 0, dt_fini_arraysz = 0;
};

// Parse and map an image without touching its relocations, and report what its dynamic table
// says. The linker needs the dynamic table; dh2run needs only the segments.
bool map_elf(GuestMemory& mem, const std::string& path, std::uint32_t dyn_limit, LoadedImage& out,
             std::string& error);

// Read the dynamic table that map_elf located and fill the dt_* fields, the soname and the
// DT_NEEDED list. Must be called after map_elf, because it reads through guest memory.
bool parse_dynamic(GuestMemory& mem, LoadedImage& out, std::string& error);

// A printable name for an R_ARM_* relocation type, for reports.
const char* arm_reloc_name(std::uint32_t type);

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
