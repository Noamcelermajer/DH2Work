#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "zb/process.h"
#include "zb/runtime_report.h"

extern char** environ;

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: zbrun [--sysroot DIR] [--env NAME=VALUE]... <arm32-executable> [args...]\n"
                 "  --sysroot DIR     arm32 Android system files (default: $ZB_SYSROOT)\n"
                 "  --report FILE     persist guest log tail, exits and register diagnostics\n"
                 "  --env NAME=VALUE  set a variable for the guest only; the host dynamic loader never\n"
                 "                    sees it (use for guest LD_DEBUG, LD_LIBRARY_PATH, ...)\n"
                 "  --precise-faults  exact guest state at memory faults (slower; also $ZB_PRECISE_FAULTS=1)\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::string sysroot;
    if (const char* env = std::getenv("ZB_SYSROOT")) sysroot = env;
    bool precise_faults = false;
    std::string report_file;

    // Host loader variables describe 64-bit host libraries; the 32-bit guest linker would try to
    // load them (Termux sets LD_PRELOAD) and fail. Pass guest ones explicitly with --env.
    std::vector<std::string> guest_envp;
    for (char** e = environ; *e != nullptr; ++e) {
        const std::string entry = *e;
        if (entry.rfind("LD_PRELOAD=", 0) == 0 || entry.rfind("LD_LIBRARY_PATH=", 0) == 0) continue;
        guest_envp.push_back(entry);
    }

    std::vector<std::pair<std::string, std::string>> aliases;
    int i = 1;
    while (i < argc && std::strncmp(argv[i], "--", 2) == 0) {
        if (std::strcmp(argv[i], "--sysroot") == 0 && i + 1 < argc) {
            sysroot = argv[i + 1];
            i += 2;
        } else if (std::strcmp(argv[i], "--report") == 0 && i + 1 < argc) {
            report_file = argv[i + 1];
            i += 2;
        } else if (std::strcmp(argv[i], "--precise-faults") == 0) {
            precise_faults = true;
            ++i;
        } else if (std::strcmp(argv[i], "--env") == 0 && i + 1 < argc && std::strchr(argv[i + 1], '=') != nullptr) {
            const std::string assignment = argv[i + 1];
            const std::string prefix = assignment.substr(0, assignment.find('=') + 1);
            std::erase_if(guest_envp, [&](const std::string& e) { return e.compare(0, prefix.size(), prefix) == 0; });
            guest_envp.push_back(assignment);
            i += 2;
        } else if (std::strcmp(argv[i], "--alias") == 0 && i + 2 < argc) {
            const std::string from = argv[i + 1], to = argv[i + 2];
            if (from.empty() || to.empty() || from.front() != '/' || to.front() != '/' ||
                from.back() != '/' || to.back() != '/') return usage();
            aliases.emplace_back(from, to);
            i += 3;
        } else {
            return usage();
        }
    }
    if (i >= argc) return usage();

    std::vector<std::string> guest_argv(argv + i, argv + argc);

    zb::Process process;
    if (!report_file.empty() && !zb::write_runtime_report_to(zb::runtime_report(), report_file)) return 2;
    process.set_sysroot(sysroot);
    for (const auto& [from, to] : aliases) process.add_path_alias(from, to);
    if (precise_faults) process.set_precise_faults(true);
    return process.run(argv[i], guest_argv, guest_envp);
}
