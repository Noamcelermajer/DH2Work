// dh2run -- execute a 32-bit ARM Linux/Android ELF under DH2Work's own host.
//
// This is the Track A counterpart of the ZettaBridge zbrun harness: same job, none of the
// ZettaBridge code. Usage:
//
//   dh2run [options] <arm32-elf> [guest args...]
//
//   --stack-mib N        guest stack size in MiB (default 8)
//   --no-precise-faults  let memory aborts be imprecise (default is precise)
//   --no-count           do not count executed instructions
//   --quiet              print nothing but what the guest itself printed
//   --dump-regs          print the register file when the guest stops
//   --trace-syscalls     print every syscall number as it is issued
//   --max-instructions N stop after N guest instructions (0 = no limit)
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
#include "dh2/loader.hpp"
#include "dh2/syscalls.hpp"

namespace {

constexpr std::uint32_t kTopOfGuestSpace = 0xFF000000u;
constexpr std::uint32_t kDynLimit = 0xF0000000u;

void usage() {
    std::fprintf(stderr,
                 "usage: dh2run [--stack-mib N] [--no-precise-faults] [--no-count] [--dump-regs]\n"
                 "              [--trace-syscalls] [--max-instructions N] <arm32-elf> [args...]\n");
}

}  // namespace

int main(int argc, char** argv) {
    bool precise_faults = true;
    bool count_instructions = true;
    bool dump_regs = false;
    bool trace_syscalls = false;
    bool quiet = false;
    std::uint64_t max_instructions = 0;
    std::uint32_t stack_size = 8u << 20;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else if (arg == "--stack-mib" && i + 1 < argc) {
            stack_size = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10)) << 20;
        } else if (arg == "--no-precise-faults") {
            precise_faults = false;
        } else if (arg == "--no-count") {
            count_instructions = false;
        } else if (arg == "--quiet") {
            quiet = true;
        } else if (arg == "--dump-regs") {
            dump_regs = true;
        } else if (arg == "--trace-syscalls") {
            trace_syscalls = true;
        } else if (arg == "--max-instructions" && i + 1 < argc) {
            max_instructions = std::strtoull(argv[++i], nullptr, 10);
        } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
            std::fprintf(stderr, "dh2run: unknown option %s\n", arg.c_str());
            usage();
            return 2;
        } else {
            positional.push_back(arg);
        }
    }
    if (positional.empty()) {
        usage();
        return 2;
    }
    const std::string guest_path = positional.front();

    dh2::GuestMemory memory;
    if (!memory.ok()) return 2;

    dh2::LoadedImage image;
    std::string error;
    if (!dh2::load_elf32(memory, guest_path, kDynLimit, image, error)) {
        std::fprintf(stderr, "dh2run: %s\n", error.c_str());
        return 2;
    }
    if (!quiet) std::printf("dh2run: %s\n", guest_path.c_str());
    if (!quiet) {
        std::printf("  image       : %s bias=0x%08x entry=0x%08x phdr=0x%08x range=[0x%08x, 0x%08x)\n",
                    image.is_dyn ? "ET_DYN" : "ET_EXEC", image.bias, image.entry, image.phdr, image.load_start,
                    image.load_end);
        if (!image.interp.empty()) std::printf("  interp      : %s\n", image.interp.c_str());
    }

    const std::uint32_t stack_top = kTopOfGuestSpace;
    const std::uint32_t stack_base = stack_top - stack_size;
    if (!memory.map_anon(stack_base, stack_size, PROT_READ | PROT_WRITE)) {
        std::fprintf(stderr, "dh2run: cannot map the guest stack\n");
        return 2;
    }

    std::vector<std::string> guest_argv;
    guest_argv.push_back(guest_path);
    for (std::size_t i = 1; i < positional.size(); ++i) guest_argv.push_back(positional[i]);

    const std::vector<std::string> guest_env = {
        "ANDROID_ROOT=/system",
        "ANDROID_DATA=/data",
        "PATH=/system/bin",
    };

    const std::vector<dh2::AuxVector> auxv = {
        {AT_PHDR, image.phdr},      {AT_PHENT, image.phent},
        {AT_PHNUM, image.phnum},    {AT_PAGESZ, dh2::kPageSize},
        {AT_ENTRY, image.entry},    {AT_BASE, 0},
        {AT_UID, 0},                {AT_EUID, 0},
        {AT_GID, 0},                {AT_EGID, 0},
        {AT_HWCAP, 0},              {AT_CLKTCK, 100},
        {AT_SECURE, 0},
    };

    const std::uint32_t sp =
        dh2::build_initial_stack(memory, stack_top, guest_argv, guest_env, auxv, guest_path);
    if (sp == 0) {
        std::fprintf(stderr, "dh2run: cannot build the initial stack image\n");
        return 2;
    }
    if (!quiet) {
        std::printf("  stack       : base=0x%08x top=0x%08x sp=0x%08x argc=%zu\n", stack_base, stack_top, sp,
                    guest_argv.size());
    }

    dh2::Cpu cpu(memory, 0, precise_faults, count_instructions);
    std::uint32_t* regs = cpu.jit().Regs().data();
    for (int i = 0; i < 16; ++i) regs[i] = 0;
    regs[13] = sp;
    regs[15] = image.entry & ~1u;
    std::uint32_t cpsr = 0x10 | 0x80 | 0x40;  // User mode, IRQ and FIQ masked
    if ((image.entry & 1u) != 0) cpsr |= 0x20;
    cpu.jit().SetCpsr(cpsr);

    if (trace_syscalls) {
        std::printf("  trace       : every syscall is printed as it is issued\n");
    }

    dh2::SyscallLayer syscalls(memory, cpu, image);
    int exit_code = 0;
    bool exited = false;

    for (;;) {
        const dh2::Stop stop = cpu.run();
        if (stop.kind == dh2::StopKind::Svc) {
            if (trace_syscalls) {
                const std::int32_t number = stop.svc != 0 ? static_cast<std::int32_t>(stop.svc)
                                                          : static_cast<std::int32_t>(regs[7]);
                std::printf("  [svc] %-22s #%d r0=0x%08x r1=0x%08x r2=0x%08x\n",
                            dh2::syscall_name(number), number, regs[0], regs[1], regs[2]);
            }
            const dh2::SyscallResult result = syscalls.handle(stop.svc);
            if (result.kind == dh2::SyscallResult::Kind::Exit) {
                exit_code = result.exit_code;
                exited = true;
                if (dump_regs) {
                    for (int i = 0; i < 16; ++i) {
                        std::printf("    r%-2d = 0x%08x%s", i, regs[i], (i % 4 == 3) ? "\n" : "  ");
                    }
                    std::printf("    stack word(s) from sp 0x%08x:\n", regs[13]);
                    for (int i = 0; i < 48; ++i) {
                        std::uint32_t word = 0;
                        if (!memory.accessible(regs[13] + i * 4, 4, dh2::kPageRead)) break;
                        std::memcpy(&word, memory.base() + regs[13] + i * 4, 4);
                        std::printf("      [sp+%3d] 0x%08x\n", i * 4, word);
                    }
                }
                break;
            }
            if (max_instructions != 0 && cpu.instruction_count() >= max_instructions) {
                std::printf("  stop        : the instruction budget of %" PRIu64 " was reached\n",
                            max_instructions);
                exit_code = 3;
                break;
            }
            continue;
        }
        if (!quiet) std::printf("  stop        : %s\n", stop.describe().c_str());
        std::fflush(stdout);
        if (dump_regs) {
            for (int i = 0; i < 16; ++i) {
                std::printf("    r%-2d = 0x%08x%s", i, regs[i], (i % 4 == 3) ? "\n" : "  ");
            }
            std::printf("    cpsr= 0x%08x fpscr=0x%08x tp=0x%08x\n", cpu.jit().Cpsr(), cpu.jit().Fpscr(),
                        cpu.thread_pointer());
        }
        exit_code = 3;
        break;
    }

    if (!quiet) {
        std::printf("  run         : %" PRIu64 " instruction(s), %" PRIu64 " svc(s)\n",
                    cpu.instruction_count(), cpu.svc_count());
        syscalls.print_census(stdout);
        std::printf("  exit        : %s%d\n", exited ? "code " : "stopped (", exited ? exit_code : 3);
        std::fflush(stdout);
    }
    return exited ? (exit_code & 0xFF) : 3;
}
