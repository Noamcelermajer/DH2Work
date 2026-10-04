// dh2link -- load a real ARM32 shared object and its dependency closure with DH2Work's own
// guest linker, and report what happened. This is the gate that has to close before the game's
// own constructors can run at all:
//
//   dh2link --sysroot <dir> --stub <libDungeonHunter2.so>
//
//   --sysroot DIR     a directory to search for DT_NEEDED names (repeatable)
//   --stub            generate a stub for a symbol nothing defines (GLES/EGL, not implemented)
//   --quiet           print only the summary
//   --list-unresolved print every unresolved symbol, not just the count
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "dh2/guest_memory.hpp"
#include "dh2/linker.hpp"

namespace {
constexpr std::uint32_t kDynLimit = 0xF0000000u;
}

int main(int argc, char** argv) {
    std::vector<std::string> search_paths;
    std::vector<std::string> roots;
    bool stubs = false;
    bool quiet = false;
    bool list_unresolved = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--sysroot" && i + 1 < argc) {
            search_paths.push_back(argv[++i]);
        } else if (arg == "--stub") {
            stubs = true;
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--list-unresolved") {
            list_unresolved = true;
        } else {
            roots.push_back(arg);
        }
    }
    if (roots.empty()) {
        std::fprintf(stderr,
                     "usage: dh2link [--sysroot DIR]... [--stub] [--quiet] [--list-unresolved] <root.so>\n");
        return 2;
    }

    dh2::GuestMemory memory;
    if (!memory.ok()) return 2;

    dh2::GuestLinker linker(memory, kDynLimit);
    for (const std::string& path : search_paths) linker.add_search_path(path);
    linker.enable_stubs(stubs);

    dh2::LinkReport report;
    std::string error;
    const bool loaded = linker.load(roots.front(), report, error);
    if (!loaded) {
        std::fprintf(stderr, "dh2link: %s\n", error.c_str());
        return 2;
    }

    if (!quiet) {
        std::printf("dh2link: %s\n", roots.front().c_str());
        std::printf("  modules      : %zu loaded\n", report.load_order.size());
        for (std::size_t i = 0; i < linker.modules().size(); ++i) {
            const dh2::LoadedImage& image = linker.modules()[i];
            std::printf("      [%zu] %-28s bias=0x%08x [0x%08x, 0x%08x) needed=%zu\n", i,
                        image.soname.c_str(), image.bias, image.load_start, image.load_end,
                        image.needed.size());
        }
        if (!report.missing_libraries.empty()) {
            std::printf("  not found    : %zu\n", report.missing_libraries.size());
            for (const std::string& name : report.missing_libraries) {
                std::printf("      %s\n", name.c_str());
            }
        }
    }

    const dh2::RelocationCensus& census = report.relocations;
    // "seen" is everything the walkers met, so it has to include the TLS forms too; otherwise the
    // applied count can exceed it, which reads like a bug in the report rather than a category
    // that was left out of it.
    const std::uint64_t seen = census.total() + report.tls_relocations;
    std::printf("  relocations  : %llu applied of %llu seen (RELATIVE %llu, ABS32 %llu, GLOB_DAT %llu, "
                "JUMP_SLOT %llu, TLS %llu, NONE %llu, other %llu)\n",
                static_cast<unsigned long long>(report.applied), static_cast<unsigned long long>(seen),
                static_cast<unsigned long long>(census.relative),
                static_cast<unsigned long long>(census.abs32),
                static_cast<unsigned long long>(census.glob_dat),
                static_cast<unsigned long long>(census.jump_slot),
                static_cast<unsigned long long>(report.tls_relocations),
                static_cast<unsigned long long>(census.none),
                static_cast<unsigned long long>(census.other));
    if (report.packed_relocations != 0) {
        std::printf("  packed       : %llu relocation(s) from DT_ANDROID_REL / DT_ANDROID_RELR / DT_RELR\n",
                    static_cast<unsigned long long>(report.packed_relocations));
    }
    std::printf("  symbols      : %zu distinct names indexed\n", report.indexed_symbols);
    std::printf("  static TLS   : %llu byte(s) laid out below the thread pointer, %llu TLS relocation(s)\n",
                static_cast<unsigned long long>(report.static_tls_size),
                static_cast<unsigned long long>(report.tls_relocations));
    if (!report.stubbed.empty()) {
        std::printf("  stubs        : %zu symbol(s) satisfied by a generated stub\n", report.stubbed.size());
    }
    std::printf("  init_array   : %llu entry(ies) at 0x%08x\n",
                static_cast<unsigned long long>(report.init_array_entries), report.init_array);
    std::printf("  unresolved   : %zu\n", report.unresolved.size());
    if (list_unresolved) {
        for (const std::string& name : report.unresolved) std::printf("      %s\n", name.c_str());
    }
    if (!report.unsupported.empty()) {
        std::printf("  unsupported  : %zu relocation(s)\n", report.unsupported.size());
        for (std::size_t i = 0; i < report.unsupported.size() && i < 10; ++i) {
            std::printf("      %s\n", report.unsupported[i].c_str());
        }
    }
    std::printf("  verdict      : %s\n", report.ok() ? "every relocation applied, nothing unresolved"
                                                    : "INCOMPLETE");
    std::fflush(stdout);
    return report.ok() ? 0 : 1;
}
