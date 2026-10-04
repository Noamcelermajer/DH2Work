#include "dh2/linker.hpp"

#include <elf.h>
#include <sys/mman.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace dh2 {

namespace {

// The basename of a path, which is how DT_NEEDED names match a soname.
std::string basename_of(const std::string& path) {
    const std::size_t cut = path.find_last_of("/\\");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}

std::uint8_t symbol_binding(std::uint8_t info) { return info >> 4; }

}  // namespace

GuestLinker::GuestLinker(GuestMemory& memory, std::uint32_t dyn_limit)
    : mem_(memory), dyn_limit_(dyn_limit) {}

void GuestLinker::add_search_path(const std::string& directory) { search_paths_.push_back(directory); }

std::string GuestLinker::locate(const std::string& name) const {
    const std::string base = basename_of(name);
    for (const std::string& directory : search_paths_) {
        const std::string candidate = directory + "/" + base;
        std::ifstream probe(candidate, std::ios::binary);
        if (probe.good()) return candidate;
    }
    return {};
}

std::size_t GuestLinker::load_one(const std::string& path, std::string& error) {
    LoadedImage image;
    if (!map_elf(mem_, path, dyn_limit_, image, error)) return static_cast<std::size_t>(-1);
    if (!parse_dynamic(mem_, image, error)) return static_cast<std::size_t>(-1);
    if (image.soname.empty()) image.soname = basename_of(path);
    loaded_by_name_[image.soname] = modules_.size();
    loaded_by_name_[basename_of(path)] = modules_.size();
    modules_.push_back(image);
    return modules_.size() - 1;
}

void GuestLinker::index_symbols() {
    for (std::size_t m = 0; m < modules_.size(); ++m) {
        const LoadedImage& image = modules_[m];
        if (image.dt_symtab == 0 || image.dt_nsyms == 0) continue;
        const std::uint32_t symtab = image.dt_symtab + image.bias;
        const std::uint32_t syment = image.dt_syment != 0 ? image.dt_syment : sizeof(Elf32_Sym);
        for (std::uint32_t i = 1; i < image.dt_nsyms; ++i) {
            const std::uint8_t* raw = mem_.host_ptr(symtab + i * syment, sizeof(Elf32_Sym), kPageRead);
            if (raw == nullptr) break;
            Elf32_Sym symbol{};
            std::memcpy(&symbol, raw, sizeof symbol);
            if (symbol.st_shndx == SHN_UNDEF || symbol.st_name == 0) continue;
            const std::uint8_t binding = symbol_binding(symbol.st_info);
            if (binding != STB_GLOBAL && binding != STB_WEAK) continue;
            const std::uint8_t* name = mem_.host_ptr(image.dt_strtab + image.bias + symbol.st_name, 1, kPageRead);
            if (name == nullptr) continue;
            const std::string text(reinterpret_cast<const char*>(name));
            index_[text].push_back(Definition{m, symbol.st_value + image.bias, symbol.st_info});
        }
    }
}

std::uint32_t GuestLinker::find_symbol(const std::string& name) const {
    const auto it = index_.find(name);
    if (it == index_.end() || it->second.empty()) return 0;
    return it->second.front().value;
}

std::uint32_t GuestLinker::stub_for(const std::string& name) {
    const auto existing = stubs_.find(name);
    if (existing != stubs_.end()) return existing->second;
    if (stub_base_ == 0) {
        stub_base_ = mem_.find_free(kPageSize, dyn_limit_);
        if (stub_base_ == 0 || !mem_.map_anon(stub_base_, kPageSize, PROT_READ | PROT_WRITE)) {
            stub_base_ = 0;
            return 0;
        }
        stub_next_ = 0;
    }
    if (stub_next_ + 8 > kPageSize) return 0;
    const std::uint32_t address = stub_base_ + stub_next_;
    // "svc #(0x400 + index) ; bx lr". The host answers by name, which is what turns 92 blind
    // imports into a marshalling table: every GL and EGL entry point the engine imports lands
    // here, and the SVC immediate says which one it is.
    const std::uint32_t svc = 0xEF000000u | (kStubSvcBase + static_cast<std::uint32_t>(stub_names_.size()));
    const std::uint32_t body[2] = {svc, 0xE12FFF1Eu};
    if (!mem_.copy_in(address, body, sizeof body)) return 0;
    stub_names_.push_back(name);
    stub_next_ += 8;
    stubs_[name] = address;
    return address;
}

std::uint32_t GuestLinker::resolve(std::size_t module, std::uint32_t symbol_index, LinkReport& report) {
    if (symbol_index == 0) return 0;  // STN_UNDEF: the addend alone is the value
    const LoadedImage& image = modules_[module];
    if (image.dt_symtab == 0) return 0;
    const std::uint32_t syment = image.dt_syment != 0 ? image.dt_syment : sizeof(Elf32_Sym);
    const std::uint8_t* raw =
        mem_.host_ptr(image.dt_symtab + image.bias + symbol_index * syment, sizeof(Elf32_Sym), kPageRead);
    if (raw == nullptr) {
        report.unresolved.push_back(image.soname + ": symbol index " + std::to_string(symbol_index) + " is unreadable");
        return 0;
    }
    Elf32_Sym symbol{};
    std::memcpy(&symbol, raw, sizeof symbol);

    if (symbol.st_shndx != SHN_UNDEF) return symbol.st_value + image.bias;  // defined here

    std::string name;
    if (symbol.st_name != 0) {
        const std::uint8_t* text = mem_.host_ptr(image.dt_strtab + image.bias + symbol.st_name, 1, kPageRead);
        if (text != nullptr) name = std::string(reinterpret_cast<const char*>(text));
    }

    if (interposer_ != nullptr) {
        const std::uint32_t interposed = interposer_(interposer_context_, name);
        if (interposed != 0) return interposed;
    }

    const auto found = index_.find(name);
    if (found != index_.end() && !found->second.empty()) {
        // A weak definition never overrides a strong one: prefer a GLOBAL definition if present.
        for (const Definition& candidate : found->second) {
            if (symbol_binding(candidate.info) == STB_GLOBAL) return candidate.value;
        }
        return found->second.front().value;
    }

    if (symbol_binding(symbol.st_info) == STB_WEAK) return 0;  // a weak undefined symbol is 0

    if (stubs_enabled_) {
        const std::uint32_t stub = stub_for(name);
        if (stub != 0) {
            report.stubbed.push_back(image.soname + ": " + name);
            return stub;
        }
    }
    report.unresolved.push_back(image.soname + ": " + (name.empty() ? "(unnamed)" : name));
    return 0;
}

bool GuestLinker::relocate(LinkReport& report, std::string& error) {
    for (std::size_t m = 0; m < modules_.size(); ++m) {
        const LoadedImage& image = modules_[m];
        if (image.dt_relent != 0 && image.dt_relent != sizeof(Elf32_Rel)) {
            error = image.soname + ": unsupported DT_RELENT " + std::to_string(image.dt_relent);
            return false;
        }
        struct Table {
            std::uint32_t address;
            std::uint32_t size;
        };
        const Table tables[2] = {{image.dt_rel, image.dt_relsz}, {image.dt_jmprel, image.dt_pltrelsz}};
        for (const Table& table : tables) {
            if (table.address == 0 || table.size == 0) continue;
            const std::uint8_t* base = mem_.host_ptr(table.address + image.bias, table.size, kPageRead);
            if (base == nullptr) {
                error = image.soname + ": relocation table is not inside the loaded image";
                return false;
            }
            for (std::uint32_t off = 0; off + sizeof(Elf32_Rel) <= table.size; off += sizeof(Elf32_Rel)) {
                Elf32_Rel relocation{};
                std::memcpy(&relocation, base + off, sizeof relocation);
                if (!apply_relocation(m, ELF32_R_TYPE(relocation.r_info), ELF32_R_SYM(relocation.r_info),
                                      relocation.r_offset + image.bias, report, error)) {
                    return false;
                }
            }
        }
    }
    if (!relocate_packed(report, error)) return false;
    return true;
}

bool GuestLinker::relocate_packed(LinkReport& report, std::string& error) {
    // APS2: a 4-byte magic, then groups of SLEB128 deltas. Reader follows bionic's
    // AndroidPackedRelocationSection.
    const auto sleb128 = [](const std::uint8_t*& p, const std::uint8_t* end, bool& ok) -> std::int64_t {
        std::int64_t value = 0;
        int shift = 0;
        std::uint8_t byte = 0;
        do {
            if (p >= end || shift > 63) {
                ok = false;
                return 0;
            }
            byte = *p++;
            value |= static_cast<std::int64_t>(byte & 0x7f) << shift;
            shift += 7;
        } while ((byte & 0x80) != 0);
        if (shift < 64 && (byte & 0x40) != 0) value |= -(static_cast<std::int64_t>(1) << shift);
        return value;
    };

    for (std::size_t m = 0; m < modules_.size(); ++m) {
        const LoadedImage& image = modules_[m];

        if (image.dt_android_rel != 0 && image.dt_android_relsz != 0) {
            const std::uint8_t* base =
                mem_.host_ptr(image.dt_android_rel + image.bias, image.dt_android_relsz, kPageRead);
            if (base == nullptr) {
                error = image.soname + ": packed relocation section is not readable";
                return false;
            }
            const std::uint8_t* p = base;
            const std::uint8_t* end = base + image.dt_android_relsz;
            if (image.dt_android_relsz < 4 || std::memcmp(p, "APS2", 4) != 0) {
                error = image.soname + ": packed relocation section has no APS2 magic";
                return false;
            }
            p += 4;
            // Header: a declared relocation count followed by one further header field. We do not
            // need the second field to decode, but the declared count and exact stream
            // consumption are both checked below, so a different layout cannot pass silently.
            bool decoded = true;
            const std::int64_t declared = sleb128(p, end, decoded);
            (void)sleb128(p, end, decoded);
            if (!decoded) {
                error = image.soname + ": truncated packed relocation header";
                return false;
            }
            std::uint32_t offset = 0;
            std::uint64_t decoded_count = 0;
            while (p < end) {
                const std::int64_t group_size = sleb128(p, end, decoded);
                if (!decoded) break;
                if (group_size == 0) break;
                const std::int64_t group_flags = sleb128(p, end, decoded);
                if (!decoded) break;
                std::uint32_t group_r_info = 0;
                std::uint32_t group_offset_delta = 0;
                if ((group_flags & 1) != 0) group_r_info = static_cast<std::uint32_t>(sleb128(p, end, decoded));
                if ((group_flags & 2) != 0) group_offset_delta = static_cast<std::uint32_t>(sleb128(p, end, decoded));
                if ((group_flags & 4) != 0) (void)sleb128(p, end, decoded);  // grouped addend (RELA only)
                for (std::int64_t i = 0; i < group_size && decoded; ++i) {
                    const std::int64_t delta = (group_flags & 2) != 0 ? group_offset_delta : sleb128(p, end, decoded);
                    offset += static_cast<std::uint32_t>(delta);
                    const std::uint32_t r_info =
                        (group_flags & 1) != 0 ? group_r_info : static_cast<std::uint32_t>(sleb128(p, end, decoded));
                    if ((group_flags & 8) != 0 && (group_flags & 4) == 0) (void)sleb128(p, end, decoded);
                    if (!apply_relocation(m, ELF32_R_TYPE(r_info), ELF32_R_SYM(r_info), offset + image.bias,
                                          report, error)) {
                        return false;
                    }
                    ++report.packed_relocations;
                    ++decoded_count;
                }
            }
            if (!decoded || p != end || decoded_count != static_cast<std::uint64_t>(declared)) {
                error = image.soname + ": the packed relocation stream did not decode exactly (" +
                        std::to_string(decoded_count) + " of " + std::to_string(declared) + " declared)";
                return false;
            }
        }

        struct RelrSection {
            std::uint32_t address;
            std::uint32_t size;
        };
        const RelrSection relr_sections[2] = {{image.dt_android_relr, image.dt_android_relrsz},
                                              {image.dt_relr, image.dt_relrsz}};
        for (const RelrSection& section : relr_sections) {
            if (section.address == 0 || section.size < 4) continue;
            const std::uint32_t* entries = reinterpret_cast<const std::uint32_t*>(
                mem_.host_ptr(section.address + image.bias, section.size, kPageRead));
            if (entries == nullptr) {
                error = image.soname + ": RELR section is not readable";
                return false;
            }
            const std::uint32_t count = section.size / 4;
            std::uint32_t where = 0;
            for (std::uint32_t i = 0; i < count; ++i) {
                const std::uint32_t entry = entries[i];
                if ((entry & 1u) == 0) {
                    where = entry;
                    if (!apply_relocation(m, R_ARM_RELATIVE, 0, where + image.bias, report, error)) return false;
                    ++report.packed_relocations;
                    where += 4;
                } else {
                    for (unsigned bit = 1; bit < 32; ++bit) {
                        if ((entry & (1u << bit)) != 0) {
                            const std::uint32_t slot = where + (bit - 1) * 4 + image.bias;
                            if (!apply_relocation(m, R_ARM_RELATIVE, 0, slot, report, error)) return false;
                            ++report.packed_relocations;
                        }
                    }
                    where += 31 * 4;
                }
            }
        }
    }
    return true;
}

bool GuestLinker::apply_relocation(std::size_t m, std::uint32_t type, std::uint32_t symbol_index,
                                   std::uint32_t slot, LinkReport& report, std::string& error) {
    const LoadedImage& image = modules_[m];
    const auto read_slot = [&](std::uint32_t* value) -> bool {
                    const std::uint8_t* source = mem_.host_ptr(slot, 4, kPageRead);
                    if (source == nullptr) return false;
                    std::memcpy(value, source, 4);
                    return true;
                };

                switch (type) {
                    case R_ARM_NONE:
                        ++report.relocations.none;
                        break;
                    case R_ARM_RELATIVE: {
                        std::uint32_t value = 0;
                        if (!read_slot(&value)) {
                            error = image.soname + ": R_ARM_RELATIVE at an unmapped address";
                            return false;
                        }
                        if (!mem_.store32(slot, value + image.bias)) {
                            error = image.soname + ": R_ARM_RELATIVE at an unwritable address";
                            return false;
                        }
                        ++report.relocations.relative;
                        ++report.applied;
                        break;
                    }
                    case R_ARM_ABS32: {
                        std::uint32_t addend = 0;
                        if (!read_slot(&addend)) {
                            error = image.soname + ": R_ARM_ABS32 at an unmapped address";
                            return false;
                        }
                        const std::uint32_t value = resolve(m, symbol_index, report) + addend;
                        if (!mem_.store32(slot, value)) {
                            error = image.soname + ": R_ARM_ABS32 at an unwritable address";
                            return false;
                        }
                        ++report.relocations.abs32;
                        ++report.applied;
                        break;
                    }
                    case R_ARM_GLOB_DAT:
                    case R_ARM_JUMP_SLOT: {
                        const std::uint32_t value = resolve(m, symbol_index, report);
                        if (!mem_.store32(slot, value)) {
                            error = image.soname + ": a " + arm_reloc_name(type) + " slot is unwritable";
                            return false;
                        }
                        ++(type == R_ARM_GLOB_DAT ? report.relocations.glob_dat : report.relocations.jump_slot);
                        ++report.applied;
                        break;
                    }
                    case R_ARM_TLS_TPOFF32:
                    case R_ARM_TLS_DTPOFF32:
                    case R_ARM_TLS_DTPMOD32: {
                        // ARM's variant: a module's TLS block sits below the thread pointer, so a
                        // symbol's offset from TP is (its offset inside the block) - tls_total.
                        const std::uint8_t* raw =
                            mem_.host_ptr(image.dt_symtab + image.bias +
                                              symbol_index * (image.dt_syment != 0 ? image.dt_syment : sizeof(Elf32_Sym)),
                                          sizeof(Elf32_Sym), kPageRead);
                        std::uint32_t symbol_value = 0;
                        if (symbol_index != 0 && raw != nullptr && image.dt_symtab != 0) {
                            Elf32_Sym symbol{};
                            std::memcpy(&symbol, raw, sizeof symbol);
                            symbol_value = symbol.st_value;
                        }
                        std::uint32_t addend = 0;
                        if (!read_slot(&addend)) {
                            error = image.soname + ": a TLS relocation is at an unmapped address";
                            return false;
                        }
                        std::uint32_t value = 0;
                        if (type == R_ARM_TLS_DTPMOD32) {
                            value = 1;  // module id; this host loads no dynamic-TLS modules
                        } else if (type == R_ARM_TLS_DTPOFF32) {
                            value = symbol_value + addend;
                        } else {
                            value = tls_offsets_[m] + symbol_value - tls_total_ + addend;
                        }
                        if (!mem_.store32(slot, value)) {
                            error = image.soname + ": a TLS relocation slot is unwritable";
                            return false;
                        }
                        ++report.tls_relocations;
                        ++report.applied;
                        break;
                    }
                    default: {
                        ++report.relocations.other;
                        char buffer[160];
                        std::snprintf(buffer, sizeof buffer, "%s: %s (symbol %u) at 0x%08x", image.soname.c_str(),
                                      arm_reloc_name(type), symbol_index, slot);
                        report.unsupported.push_back(buffer);
                        break;
                    }
                }
    return true;
}

bool GuestLinker::load(const std::string& root, LinkReport& report, std::string& error) {
    const std::size_t root_index = load_one(root, error);
    if (root_index == static_cast<std::size_t>(-1)) return false;

    // Breadth-first over DT_NEEDED, exactly like a real linker: the root first, then each
    // dependency it names, then theirs.
    for (std::size_t i = 0; i < modules_.size(); ++i) {
        const std::vector<std::string> needed = modules_[i].needed;
        for (const std::string& name : needed) {
            const std::string base = basename_of(name);
            if (loaded_by_name_.count(base) != 0) continue;
            const std::string path = locate(name);
            if (path.empty()) {
                report.missing_libraries.push_back(base + " (needed by " + modules_[i].soname + ")");
                continue;
            }
            if (load_one(path, error) == static_cast<std::size_t>(-1)) return false;
        }
    }

    // Static TLS: lay every module's PT_TLS block out consecutively, below the thread pointer.
    {
        std::uint32_t cursor = 0;
        std::uint32_t alignment = 1;
        for (const LoadedImage& image : modules_) {
            if (image.tls_memsz != 0 && image.tls_align > alignment) alignment = image.tls_align;
        }
        tls_offsets_.assign(modules_.size(), 0);
        for (std::size_t i = 0; i < modules_.size(); ++i) {
            const LoadedImage& image = modules_[i];
            if (image.tls_memsz == 0) continue;
            const std::uint32_t align = image.tls_align != 0 ? image.tls_align : 1;
            cursor = (cursor + align - 1) / align * align;
            tls_offsets_[i] = cursor;
            cursor += image.tls_memsz;
        }
        // The thread pointer sits at the end of the static block and must be 16-byte aligned,
        // and libc.so's own 8-byte block has to end exactly there, so the total is rounded to 16
        // rather than to the largest module alignment.
        if (alignment < 16) alignment = 16;
        tls_total_ = (cursor + alignment - 1) / alignment * alignment;
        report.static_tls_size = tls_total_;
    }

    index_symbols();
    report.indexed_symbols = index_.size();

    for (const LoadedImage& image : modules_) {
        report.load_order.push_back(image.soname);
    }
    report.entry = modules_[root_index].entry;
    if (modules_[root_index].dt_init_array != 0 && modules_[root_index].dt_init_arraysz != 0) {
        report.init_array = modules_[root_index].dt_init_array + modules_[root_index].bias;
        report.init_array_entries = modules_[root_index].dt_init_arraysz / 4;
    }

    if (!relocate(report, error)) return false;

    // The generated stubs live on one page; make it executable now that it is written.
    if (stub_base_ != 0) {
        if (!mem_.protect(stub_base_, kPageSize, PROT_READ | PROT_EXEC)) {
            error = "cannot make the stub page executable";
            return false;
        }
    }
    return true;
}

}  // namespace dh2
