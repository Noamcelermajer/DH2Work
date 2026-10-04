// dh2boot -- load a shared-object closure with our own linker and run the closure's own
// initializers (DT_INIT and DT_INIT_ARRAY) under our own JIT.
//
// dh2link proves the relocations apply. This is the next gate: the engine is only "started" when
// its constructors actually run, and for that everything they call has to work -- the loader
// interface libc.so imports from the platform linker, the initial thread's TLS control block,
// the kuser helper page, and the syscall slice.
//
//   dh2boot --sysroot DIR [--stub] [--skip-dependency-init] [--max-instructions N] <root.so>
#include <elf.h>
#include <sys/mman.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "dh2/cpu.hpp"
#include "dh2/initial_stack.hpp"
#include "dh2/linker.hpp"
#include "dh2/loader_if.hpp"
#include "dh2/syscalls.hpp"

namespace {

constexpr std::uint32_t kDynLimit = 0xF0000000u;
constexpr std::uint32_t kStackTop = 0xFF000000u;
constexpr std::uint32_t kStackSize = 8u << 20;
constexpr std::uint32_t kReturnSwi = 0x7f;      // reserved: a guest returning to the host
constexpr std::uint32_t kKuserPage = 0xFFFF0000u;

constexpr std::uint16_t kThumbReturn = 0xDF7Fu;
constexpr std::uint32_t kArmReturn = 0xEF00007Fu;

std::uint32_t interpose(void* context, const std::string& name) {
    return static_cast<dh2::LoaderInterface*>(context)->stub_for(name);
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> search_paths;
    std::vector<std::string> roots;
    bool stubs = false;
    bool skip_dependency_init = false;
    bool deps_first = false;
    std::uint64_t max_instructions = 0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--sysroot" && i + 1 < argc) {
            search_paths.push_back(argv[++i]);
        } else if (arg == "--stub") {
            stubs = true;
        } else if (arg == "--skip-dependency-init") {
            skip_dependency_init = true;
        } else if (arg == "--deps-first") {
            deps_first = true;
        } else if (arg == "--max-instructions" && i + 1 < argc) {
            max_instructions = std::strtoull(argv[++i], nullptr, 10);
        } else {
            roots.push_back(arg);
        }
    }
    if (roots.empty()) {
        std::fprintf(stderr,
                     "usage: dh2boot --sysroot DIR [--stub] [--skip-dependency-init] <root.so>\n");
        return 2;
    }

    dh2::GuestMemory memory;
    if (!memory.ok()) return 2;
    if (!memory.map_anon(kStackTop - kStackSize, kStackSize, PROT_READ | PROT_WRITE)) {
        std::fprintf(stderr, "dh2boot: cannot map the guest stack\n");
        return 2;
    }

    // The initial argument block, so libc can find argc/argv and the auxv it walks.
    const std::vector<std::string> guest_argv = {roots.front()};
    const std::vector<std::string> guest_env = {"ANDROID_ROOT=/system", "PATH=/system/bin"};
    const std::vector<dh2::AuxVector> auxv = {
        {AT_PAGESZ, dh2::kPageSize}, {AT_ENTRY, 0}, {AT_BASE, 0}, {AT_UID, 0}, {AT_EUID, 0},
        {AT_GID, 0},                 {AT_EGID, 0}, {AT_HWCAP, 0}, {AT_CLKTCK, 100}, {AT_SECURE, 0},
    };
    std::uint32_t auxv_address = 0;
    const std::uint32_t sp = dh2::build_initial_stack(memory, kStackTop, guest_argv, guest_env,
                                                      auxv, roots.front(), &auxv_address);
    if (sp == 0) {
        std::fprintf(stderr, "dh2boot: cannot build the initial stack\n");
        return 2;
    }

    dh2::Cpu cpu(memory, 0, true, true);
    dh2::LoaderInterface loader_if(memory, cpu, kDynLimit);
    std::string error;
    if (!loader_if.install(static_cast<std::uint32_t>(guest_argv.size()), guest_argv, auxv_address,
                           error)) {
        std::fprintf(stderr, "dh2boot: %s\n", error.c_str());
        return 2;
    }

    dh2::GuestLinker linker(memory, kDynLimit);
    for (const std::string& path : search_paths) linker.add_search_path(path);
    linker.enable_stubs(stubs);
    linker.set_interposer(&interpose, &loader_if);

    dh2::LinkReport report;
    if (!linker.load(roots.front(), report, error)) {
        std::fprintf(stderr, "dh2boot: %s\n", error.c_str());
        return 2;
    }
    std::printf("dh2boot: %s\n", roots.front().c_str());
    std::printf("  linked       : %zu module(s), %llu relocation(s) applied, %zu unresolved\n",
                linker.modules().size(), static_cast<unsigned long long>(report.applied),
                report.unresolved.size());
    std::printf("  loader iface : shared_globals=0x%08x, %zu event(s) so far\n", loader_if.globals(),
                loader_if.events().size());

    // The initial thread's control block. Bionic reads errno at TP+0x29C and its canary at
    // [TP-4], so the thread pointer must be a real pthread_internal_t positioned at the end of
    // the static TLS block, and it has to exist before the first guest instruction.
    const std::uint32_t tls_total = linker.static_tls_size();
    const std::uint32_t control_size = dh2::page_round_up(tls_total + 0x2A0);
    if (!memory.map_anon(kStackTop, control_size, PROT_READ | PROT_WRITE)) {
        std::fprintf(stderr, "dh2boot: cannot map the TLS control block\n");
        return 2;
    }
    const std::uint32_t tp = kStackTop + tls_total;
    const std::uint32_t self = tp;
    memory.copy_in(tp, &self, 4);
    memory.copy_in(tp + 4, &self, 4);
    const std::uint32_t zero = 0;
    memory.copy_in(tp + 0x29C, &zero, 4);
    cpu.cp15().set_tpidruro(tp);
    std::printf("  thread       : static TLS %u bytes, TP=0x%08x [TP]=TP [TP+4]=TP errno=0\n",
                tls_total, tp);

    // The kuser helper page: legacy bionic paths still call it.
    if (memory.map_anon(kKuserPage, dh2::kPageSize, PROT_READ | PROT_WRITE)) {
        const std::uint32_t get_tls[2] = {0xEE1D0F70u, 0xE12FFF1Eu};   // mrc p15,0,r0,c13,c0,3 ; bx lr
        const std::uint32_t barrier[2] = {0xEE070FAAu, 0xE12FFF1Eu};   // mcr p15,0,r0,c7,c10,5 ; bx lr
        const std::uint32_t version = 3;
        memory.copy_in(kKuserPage + 0x0FA0, barrier, sizeof barrier);
        memory.copy_in(kKuserPage + 0x0FE0, get_tls, sizeof get_tls);
        memory.copy_in(kKuserPage + 0x0FFC, &version, 4);
        memory.protect(kKuserPage, dh2::kPageSize, PROT_READ | PROT_EXEC);
    }

    // The return trampoline: a guest function returns to us by branching to a reserved SVC.
    const std::uint32_t trampoline = memory.find_free(dh2::kPageSize, kDynLimit);
    if (trampoline == 0 || !memory.map_anon(trampoline, dh2::kPageSize, PROT_READ | PROT_WRITE)) {
        std::fprintf(stderr, "dh2boot: cannot map the return trampoline\n");
        return 2;
    }
    const std::uint16_t thumb[2] = {kThumbReturn, 0};
    memory.copy_in(trampoline, thumb, sizeof thumb);
    memory.copy_in(trampoline + 4, &kArmReturn, sizeof kArmReturn);
    memory.protect(trampoline, dh2::kPageSize, PROT_READ | PROT_EXEC);

    dh2::SyscallLayer syscalls(memory, cpu, linker.modules().front());

    const auto call = [&](std::uint32_t target, std::uint32_t& result, dh2::Stop& stop_out) -> bool {
        std::uint32_t* regs = cpu.jit().Regs().data();
        for (int i = 0; i < 13; ++i) regs[i] = 0;
        regs[13] = kStackTop - 0x1000;
        regs[14] = trampoline | 1u;
        regs[15] = target & ~1u;
        std::uint32_t cpsr = 0x10 | 0x80 | 0x40;
        if ((target & 1u) != 0) cpsr |= 0x20;
        cpu.jit().SetCpsr(cpsr);
        for (;;) {
            const dh2::Stop stop = cpu.run();
            if (stop.kind == dh2::StopKind::Svc) {
                if (stop.svc == kReturnSwi) {
                    result = regs[0];
                    return true;
                }
                if (stop.svc == 0x0f0005) {  // __ARM_NR_set_tls
                    cpu.cp15().set_tpidruro(regs[0]);
                    regs[0] = 0;
                    continue;
                }
                if (loader_if.handle_svc(stop.svc)) continue;
                const dh2::SyscallResult outcome = syscalls.handle(stop.svc);
                if (outcome.kind == dh2::SyscallResult::Kind::Exit) {
                    stop_out = stop;
                    stop_out.kind = dh2::StopKind::Step;
                    return false;
                }
                continue;
            }
            stop_out = stop;
            return false;
        }
    };

    int completed = 0;
    bool halted = false;
    std::size_t stop_module = 0;
    std::size_t stop_index = 0;

    // The root's constructors run first by default, which is the order the project's own
    // measurement of "539 of 539" was taken in; --deps-first reverses it to the dependency-first
    // order a real linker uses.
    for (std::size_t step = 0; step < linker.modules().size() && !halted; ++step) {
        const std::size_t m = deps_first ? linker.modules().size() - 1 - step : step;
        const dh2::LoadedImage& image = linker.modules()[m];
        if (skip_dependency_init && m != 0) continue;

        std::vector<std::uint32_t> initializers;
        if (image.dt_init != 0) initializers.push_back(image.dt_init + image.bias);
        if (image.dt_init_array != 0 && image.dt_init_arraysz != 0) {
            const std::uint32_t count = image.dt_init_arraysz / 4;
            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint32_t entry = 0;
                const std::uint32_t at = image.dt_init_array + image.bias + i * 4;
                if (!memory.accessible(at, 4, dh2::kPageRead)) break;
                std::memcpy(&entry, memory.base() + at, 4);
                if (entry != 0) initializers.push_back(entry);
            }
        }
        if (initializers.empty()) continue;
        std::printf("  %-24s %zu initializer(s)\n", image.soname.c_str(), initializers.size());
        std::fflush(stdout);

        for (std::size_t i = 0; i < initializers.size(); ++i) {
            if (max_instructions != 0 && cpu.instruction_count() >= max_instructions) {
                std::printf("  stop         : instruction budget reached\n");
                halted = true;
                break;
            }
            dh2::Stop stop;
            std::uint32_t result = 0;
            if (!call(initializers[i], result, stop)) {
                std::printf("  stop         : %s [%zu] at 0x%08x (link-time 0x%08x) -- %s\n",
                            image.soname.c_str(), i, initializers[i], initializers[i] - image.bias,
                            stop.describe().c_str());
                const std::uint32_t* r = cpu.jit().Regs().data();
                std::printf("  registers    :");
                for (int k = 0; k < 16; ++k) std::printf(" r%d=0x%08x", k, r[k]);
                std::printf("\n                 cpsr=0x%08x tp=0x%08x\n", cpu.jit().Cpsr(),
                            cpu.thread_pointer());
                std::printf("  progress     : %d initializer(s) completed, %llu instruction(s)\n",
                            completed, static_cast<unsigned long long>(cpu.instruction_count()));
                stop_module = m;
                stop_index = i;
                halted = true;
                break;
            }
            ++completed;
            if ((completed % 50) == 0) {
                std::printf("  ...          : %d completed, %llu instruction(s)\n", completed,
                            static_cast<unsigned long long>(cpu.instruction_count()));
                std::fflush(stdout);
            }
        }
    }
    (void)stop_module;
    (void)stop_index;

    std::printf("  initializers : %d completed\n", completed);
    std::printf("  instructions : %llu\n", static_cast<unsigned long long>(cpu.instruction_count()));
    syscalls.print_census(stdout);
    loader_if.print_census(stdout);
    if (!loader_if.events().empty()) {
        std::printf("  loader iface : %zu event(s)\n", loader_if.events().size());
        for (const std::string& event : loader_if.events()) std::printf("      %s\n", event.c_str());
    }
    std::fflush(stdout);
    return 0;
}
