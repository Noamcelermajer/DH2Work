#include "dh2/loader.hpp"

#include <elf.h>
#include <sys/mman.h>

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

bool load_elf32(GuestMemory& mem, const std::string& path, std::uint32_t dyn_limit, LoadedImage& out,
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
    for (const auto& p : phdrs) {
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

    // Relocations. Arm uses REL (no addend field in the entry).
    const Elf32_Phdr* dynamic = nullptr;
    for (const auto& p : phdrs) {
        if (p.p_type == PT_DYNAMIC) dynamic = &p;
    }
    if (dynamic != nullptr && dynamic->p_filesz != 0) {
        const std::size_t count = dynamic->p_filesz / sizeof(Elf32_Dyn);
        const Elf32_Dyn* dyn = reinterpret_cast<const Elf32_Dyn*>(data.data() + dynamic->p_offset);
        std::uint32_t rel = 0, relsz = 0, relent = sizeof(Elf32_Rel);
        std::uint32_t jmprel = 0, pltrelsz = 0;
        for (std::size_t i = 0; i < count; ++i) {
            switch (dyn[i].d_tag) {
                case DT_REL: rel = dyn[i].d_un.d_ptr; break;
                case DT_RELSZ: relsz = dyn[i].d_un.d_val; break;
                case DT_RELENT: relent = dyn[i].d_un.d_val; break;
                case DT_JMPREL: jmprel = dyn[i].d_un.d_ptr; break;
                case DT_PLTRELSZ: pltrelsz = dyn[i].d_un.d_val; break;
                default: break;
            }
        }
        const auto apply = [&](std::uint32_t addr, std::uint32_t size) -> bool {
            if (addr == 0 || size == 0) return true;
            if (relent != sizeof(Elf32_Rel)) {
                error = path + ": unsupported DT_RELENT " + std::to_string(relent);
                return false;
            }
            // The table lives in the image we just mapped, so read it through guest memory.
            std::uint8_t* table = mem.host_ptr(addr + bias, size, kPageRead);
            if (table == nullptr) {
                error = path + ": relocation table is not inside the loaded image";
                return false;
            }
            for (std::uint32_t off = 0; off + sizeof(Elf32_Rel) <= size; off += sizeof(Elf32_Rel)) {
                Elf32_Rel r{};
                std::memcpy(&r, table + off, sizeof r);
                const std::uint8_t type = static_cast<std::uint8_t>(ELF32_R_TYPE(r.r_info));
                std::uint8_t* slot = mem.host_ptr(r.r_offset + bias, 4, kPageWrite);
                switch (type) {
                    case R_ARM_NONE:
                        break;
                    case R_ARM_RELATIVE: {
                        if (slot == nullptr) {
                            error = path + ": R_ARM_RELATIVE targets an unwritable address";
                            return false;
                        }
                        std::uint32_t value = 0;
                        std::memcpy(&value, slot, 4);
                        value += bias;
                        std::memcpy(slot, &value, 4);
                        break;
                    }
                    default:
                        error = std::string(path) + ": needs " + reloc_name(type) +
                                " (symbol " + std::to_string(ELF32_R_SYM(r.r_info)) +
                                "), which this loader does not resolve yet";
                        return false;
                }
            }
            return true;
        };
        if (!apply(rel, relsz)) return false;
        if (!apply(jmprel, pltrelsz)) return false;
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

}  // namespace dh2
