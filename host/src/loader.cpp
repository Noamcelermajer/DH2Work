#include "dh2/loader.hpp"

#include <elf.h>
#include <sys/mman.h>

// Android's packed-relocation dynamic tags are not in every system elf.h. The values are fixed
// by the platform ABI (bionic's own elf.h) and are the same for 32- and 64-bit guests.
#ifndef DT_ANDROID_REL
#define DT_ANDROID_REL 0x6000000f
#define DT_ANDROID_RELSZ 0x60000010
#define DT_ANDROID_RELA 0x60000011
#define DT_ANDROID_RELASZ 0x60000012
#endif
#ifndef DT_ANDROID_RELR
#define DT_ANDROID_RELR 0x6fffe000
#define DT_ANDROID_RELRSZ 0x6fffe001
#define DT_ANDROID_RELRENT 0x6fffe003
#endif
#ifndef DT_RELR
#define DT_RELR 36
#define DT_RELRSZ 35
#define DT_RELRENT 37
#endif

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

namespace dh2 {

namespace {

template <typename T>
T read_at(const std::vector<char>& data, std::size_t offset) {
    T v{};
    std::memcpy(&v, data.data() + offset, sizeof v);
    return v;
}

const char* reloc_name(std::uint32_t type) {
    switch (type) {
        case R_ARM_NONE: return "R_ARM_NONE";
        case R_ARM_RELATIVE: return "R_ARM_RELATIVE";
        case R_ARM_ABS32: return "R_ARM_ABS32";
        case R_ARM_GLOB_DAT: return "R_ARM_GLOB_DAT";
        case R_ARM_JUMP_SLOT: return "R_ARM_JUMP_SLOT";
        default: return "unknown";
    }
}

}  // namespace

bool map_elf(GuestMemory& mem, const std::string& path, std::uint32_t dyn_limit, LoadedImage& out,
             std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open " + path;
        return false;
    }
    const std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    if (data.size() < sizeof(Elf32_Ehdr)) {
        error = "too small to be an ELF: " + path;
        return false;
    }
    Elf32_Ehdr eh{};
    std::memcpy(&eh, data.data(), sizeof eh);
    if (std::memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 || eh.e_ident[EI_CLASS] != ELFCLASS32 ||
        eh.e_ident[EI_DATA] != ELFDATA2LSB || eh.e_machine != EM_ARM) {
        error = "not a little-endian ARM ELF32: " + path;
        return false;
    }
    if (eh.e_type != ET_EXEC && eh.e_type != ET_DYN) {
        error = "unsupported ELF type: " + path;
        return false;
    }
    if (eh.e_phentsize != sizeof(Elf32_Phdr) ||
        static_cast<std::uint64_t>(eh.e_phoff) + static_cast<std::uint64_t>(eh.e_phnum) * sizeof(Elf32_Phdr) >
            data.size()) {
        error = "bad program header table in " + path;
        return false;
    }
    std::vector<Elf32_Phdr> phdrs(eh.e_phnum);
    std::memcpy(phdrs.data(), data.data() + eh.e_phoff, phdrs.size() * sizeof(Elf32_Phdr));

    std::uint64_t lo = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t hi = 0;
    const Elf32_Phdr* dynamic_segment = nullptr;
    for (const auto& p : phdrs) {
        if (p.p_type == PT_DYNAMIC) dynamic_segment = &p;
        if (p.p_type != PT_LOAD) continue;
        if (p.p_filesz > p.p_memsz || static_cast<std::uint64_t>(p.p_offset) + p.p_filesz > data.size()) {
            error = "bad PT_LOAD in " + path;
            return false;
        }
        lo = std::min<std::uint64_t>(lo, page_round_down(p.p_vaddr));
        hi = std::max<std::uint64_t>(hi, page_round_up(static_cast<std::uint64_t>(p.p_vaddr) + p.p_memsz));
    }
    if (hi == 0 || hi > kGuestSpaceSize) {
        error = "no loadable segments in " + path;
        return false;
    }
    const std::uint64_t span = hi - lo;

    std::uint32_t bias = 0;
    if (eh.e_type == ET_DYN) {
        const std::uint32_t at = mem.find_free(span, dyn_limit);
        if (at == 0) {
            error = "no free guest address space below 0x" + std::to_string(dyn_limit) + " for " + path;
            return false;
        }
        bias = at - static_cast<std::uint32_t>(lo);
    } else if (!mem.range_free(static_cast<std::uint32_t>(lo), span)) {
        error = "the address range of " + path + " is already in use";
        return false;
    }

    // Materialise every segment writable first, then narrow the protections: guest code pages
    // must end up readable but never host-writable.
    const std::uint32_t start = static_cast<std::uint32_t>(lo) + bias;
    if (!mem.map_anon(start, span, PROT_READ | PROT_WRITE)) {
        error = "cannot map " + path + " into the guest address space";
        return false;
    }

    out = LoadedImage{};
    for (const auto& p : phdrs) {
        if (p.p_type == PT_TLS) {
            out.tls_vaddr = p.p_vaddr + bias;
            out.tls_filesz = p.p_filesz;
            out.tls_memsz = p.p_memsz;
            out.tls_align = p.p_align != 0 ? p.p_align : 1;
        }
    }
    for (const auto& p : phdrs) {
        if (p.p_type == PT_LOAD && p.p_filesz != 0) {
            if (!mem.copy_in(static_cast<std::uint32_t>(p.p_vaddr) + bias, data.data() + p.p_offset, p.p_filesz)) {
                error = "cannot copy a PT_LOAD segment of " + path;
                return false;
            }
        } else if (p.p_type == PT_INTERP) {
            if (static_cast<std::uint64_t>(p.p_offset) + p.p_filesz > data.size()) {
                error = "bad PT_INTERP in " + path;
                return false;
            }
            const char* s = data.data() + p.p_offset;
            out.interp.assign(s, strnlen(s, p.p_filesz));
        }
    }
    for (const auto& p : phdrs) {
        if (p.p_type != PT_LOAD) continue;
        const int prot = ((p.p_flags & PF_R) ? PROT_READ : 0) | ((p.p_flags & PF_W) ? PROT_WRITE : 0) |
                         ((p.p_flags & PF_X) ? PROT_EXEC : 0);
        const std::uint32_t seg_start = page_round_down(static_cast<std::uint64_t>(p.p_vaddr) + bias);
        const std::uint64_t seg_end =
            page_round_up(static_cast<std::uint64_t>(p.p_vaddr) + bias + p.p_memsz);
        if (!mem.protect(seg_start, seg_end - seg_start, prot)) {
            error = "cannot set the protections of " + path;
            return false;
        }
    }

    for (const auto& p : phdrs) {
        if (p.p_type == PT_PHDR) out.phdr = p.p_vaddr + bias;
    }
    if (out.phdr == 0) {
        for (const auto& p : phdrs) {
            if (p.p_type == PT_LOAD && eh.e_phoff >= p.p_offset && eh.e_phoff < p.p_offset + p.p_filesz) {
                out.phdr = static_cast<std::uint32_t>(p.p_vaddr) + (eh.e_phoff - p.p_offset) + bias;
                break;
            }
        }
    }

    if (dynamic_segment != nullptr && dynamic_segment->p_filesz != 0) {
        out.dynamic_addr = dynamic_segment->p_vaddr + bias;
        out.dynamic_size = dynamic_segment->p_filesz;
    }

    out.bias = bias;
    out.entry = eh.e_entry + bias;
    out.phnum = eh.e_phnum;
    out.phent = eh.e_phentsize;
    out.load_start = start;
    out.load_end = static_cast<std::uint32_t>(hi + bias);
    out.is_dyn = eh.e_type == ET_DYN;
    return true;
}

const char* arm_reloc_name(std::uint32_t type) { return reloc_name(type); }

bool parse_dynamic(GuestMemory& mem, LoadedImage& out, std::string& error) {
    if (out.dynamic_addr == 0 || out.dynamic_size == 0) return true;
    const Elf32_Dyn* dyn = reinterpret_cast<const Elf32_Dyn*>(
        mem.host_ptr(out.dynamic_addr, out.dynamic_size, kPageRead));
    if (dyn == nullptr) {
        error = "the dynamic table is not readable at runtime";
        return false;
    }
    std::vector<std::uint32_t> needed_offsets;
    std::uint32_t soname_offset = 0;
    const std::size_t slots = out.dynamic_size / sizeof(Elf32_Dyn);
    for (std::size_t i = 0; i < slots; ++i) {
        const Elf32_Dyn& e = dyn[i];
        if (e.d_tag == DT_NULL) break;
        switch (e.d_tag) {
            case DT_NEEDED: needed_offsets.push_back(e.d_un.d_val); break;
            case DT_SONAME: soname_offset = e.d_un.d_val; break;
            case DT_STRTAB: out.dt_strtab = e.d_un.d_ptr; break;
            case DT_STRSZ: out.dt_strsz = e.d_un.d_val; break;
            case DT_SYMTAB: out.dt_symtab = e.d_un.d_ptr; break;
            case DT_SYMENT: out.dt_syment = e.d_un.d_val; break;
            case DT_HASH: out.dt_hash = e.d_un.d_ptr; break;
            case DT_GNU_HASH: out.dt_gnu_hash = e.d_un.d_ptr; break;
            case DT_REL: out.dt_rel = e.d_un.d_ptr; break;
            case DT_ANDROID_REL: out.dt_android_rel = e.d_un.d_ptr; break;
            case DT_ANDROID_RELSZ: out.dt_android_relsz = e.d_un.d_val; break;
            case DT_ANDROID_RELR: out.dt_android_relr = e.d_un.d_ptr; break;
            case DT_ANDROID_RELRSZ: out.dt_android_relrsz = e.d_un.d_val; break;
            case DT_ANDROID_RELRENT: out.dt_android_relrent = e.d_un.d_val; break;
            case DT_RELR: out.dt_relr = e.d_un.d_ptr; break;
            case DT_RELRSZ: out.dt_relrsz = e.d_un.d_val; break;
            case DT_RELRENT: out.dt_relrent = e.d_un.d_val; break;
            case DT_RELSZ: out.dt_relsz = e.d_un.d_val; break;
            case DT_RELENT: out.dt_relent = e.d_un.d_val; break;
            case DT_JMPREL: out.dt_jmprel = e.d_un.d_ptr; break;
            case DT_PLTRELSZ: out.dt_pltrelsz = e.d_un.d_val; break;
            case DT_INIT: out.dt_init = e.d_un.d_ptr; break;
            case DT_FINI: out.dt_fini = e.d_un.d_ptr; break;
            case DT_INIT_ARRAY: out.dt_init_array = e.d_un.d_ptr; break;
            case DT_INIT_ARRAYSZ: out.dt_init_arraysz = e.d_un.d_val; break;
            case DT_FINI_ARRAY: out.dt_fini_array = e.d_un.d_ptr; break;
            case DT_FINI_ARRAYSZ: out.dt_fini_arraysz = e.d_un.d_val; break;
            case DT_TEXTREL: out.textrel = true; break;
            case DT_FLAGS: out.textrel = out.textrel || ((e.d_un.d_val & DF_TEXTREL) != 0); break;
            default: break;
        }
    }

    const auto string_at = [&](std::uint32_t offset) -> std::string {
        if (out.dt_strtab == 0) return {};
        if (out.dt_strsz != 0 && offset >= out.dt_strsz) return {};
        const std::uint8_t* p = mem.host_ptr(out.dt_strtab + out.bias + offset, 1, kPageRead);
        if (p == nullptr) return {};
        const std::uint32_t limit = out.dt_strsz != 0 ? out.dt_strsz - offset : (1u << 20);
        std::uint32_t length = 0;
        while (length < limit && p[length] != 0) ++length;
        return std::string(reinterpret_cast<const char*>(p), length);
    };
    if (soname_offset != 0) out.soname = string_at(soname_offset);
    for (const std::uint32_t offset : needed_offsets) {
        std::string name = string_at(offset);
        if (!name.empty()) out.needed.push_back(name);
    }

    // The symbol count: SysV hash states it; a GNU-hash-only object needs the bucket/chain walk.
    if (out.dt_hash != 0) {
        const std::uint32_t* hash = reinterpret_cast<const std::uint32_t*>(mem.host_ptr(out.dt_hash + out.bias, 8, kPageRead));
        if (hash != nullptr) out.dt_nsyms = hash[1];
    } else if (out.dt_gnu_hash != 0) {
        const std::uint32_t* gnu = reinterpret_cast<const std::uint32_t*>(mem.host_ptr(out.dt_gnu_hash + out.bias, 16, kPageRead));
        if (gnu != nullptr) {
            const std::uint32_t buckets = gnu[0];
            const std::uint32_t symoffset = gnu[1];
            const std::uint32_t bloom_size = gnu[2];
            const std::uint32_t bucket_base = out.dt_gnu_hash + out.bias + 16 + bloom_size * 4;
            const std::uint32_t* bucket_table = reinterpret_cast<const std::uint32_t*>(mem.host_ptr(bucket_base, buckets * 4, kPageRead));
            if (bucket_table != nullptr) {
                std::uint32_t highest = symoffset;
                for (std::uint32_t b = 0; b < buckets; ++b) {
                    std::uint32_t index = bucket_table[b];
                    if (index < symoffset) continue;
                    for (;;) {
                        const std::uint32_t* chain = reinterpret_cast<const std::uint32_t*>(
                            mem.host_ptr(bucket_base + buckets * 4 + (index - symoffset) * 4, 4, kPageRead));
                        if (chain == nullptr) break;
                        ++index;
                        if ((*chain & 1u) != 0) break;
                    }
                    if (index > highest) highest = index;
                }
                out.dt_nsyms = highest;
            }
        }
    }
    return true;
}

bool load_elf32(GuestMemory& mem, const std::string& path, std::uint32_t dyn_limit, LoadedImage& out,
                std::string& error) {
    if (!map_elf(mem, path, dyn_limit, out, error)) return false;
    if (!parse_dynamic(mem, out, error)) return false;
    if (out.dt_relent != 0 && out.dt_relent != sizeof(Elf32_Rel)) {
        error = path + ": unsupported DT_RELENT " + std::to_string(out.dt_relent);
        return false;
    }

    const auto apply = [&](std::uint32_t address, std::uint32_t size) -> bool {
        if (address == 0 || size == 0) return true;
        std::uint8_t* table = mem.host_ptr(address + out.bias, size, kPageRead);
        if (table == nullptr) {
            error = path + ": relocation table is not inside the loaded image";
            return false;
        }
        for (std::uint32_t off = 0; off + sizeof(Elf32_Rel) <= size; off += sizeof(Elf32_Rel)) {
            Elf32_Rel r{};
            std::memcpy(&r, table + off, sizeof r);
            const std::uint8_t type = static_cast<std::uint8_t>(ELF32_R_TYPE(r.r_info));
            switch (type) {
                case R_ARM_NONE:
                    break;
                case R_ARM_RELATIVE: {
                    const std::uint8_t* source = mem.host_ptr(r.r_offset + out.bias, 4, kPageRead);
                    if (source == nullptr) {
                        error = path + ": R_ARM_RELATIVE targets an unmapped address";
                        return false;
                    }
                    std::uint32_t value = 0;
                    std::memcpy(&value, source, 4);
                    value += out.bias;
                    if (!mem.store32(r.r_offset + out.bias, value)) {
                        error = path + ": R_ARM_RELATIVE targets an unwritable address";
                        return false;
                    }
                    break;
                }
                default:
                    error = std::string(path) + ": needs " + reloc_name(type) +
                            " (symbol " + std::to_string(ELF32_R_SYM(r.r_info)) +
                            "), which the static loader does not resolve";
                    return false;
            }
        }
        return true;
    };
    if (!apply(out.dt_rel, out.dt_relsz)) return false;
    if (!apply(out.dt_jmprel, out.dt_pltrelsz)) return false;
    return true;
}

}  // namespace dh2
