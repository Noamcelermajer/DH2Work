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

#include <sys/random.h>
#include <unistd.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "dh2/cpu.hpp"
#include "dh2/initial_stack.hpp"
#include "dh2/jni.hpp"
#include "dh2/linker.hpp"
#include "dh2/loader_if.hpp"
#include "dh2/syscalls.hpp"

namespace {

constexpr std::uint32_t kDynLimit = 0xF0000000u;
constexpr std::uint32_t kStackTop = 0xFF000000u;
constexpr std::uint32_t kStackSize = 8u << 20;
constexpr std::uint32_t kReturnSwi = 0x7f;      // reserved: a guest returning to the host
constexpr std::uint32_t kSlingshotSwi = 0x80;   // reserved: bionic called the application entry
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
    bool root_first = false;
    bool trace_steps = false;
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
        } else if (arg == "--root-first") {
            root_first = true;
        } else if (arg == "--trace-steps") {
            trace_steps = true;
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
    for (std::size_t i = 0; i < linker.modules().size(); ++i) {
        const dh2::LoadedImage& image = linker.modules()[i];
        std::printf("      [%zu] %-24s bias=0x%08x [0x%08x, 0x%08x)\n", i, image.soname.c_str(),
                    image.bias, image.load_start, image.load_end);
    }
    std::printf("  loader iface : shared_globals=0x%08x, %zu event(s) so far\n", loader_if.globals(),
                loader_if.events().size());

    // Diagnostics for the preinit path: the GOT slot and the guard it should point at.
    for (const dh2::LoadedImage& image : linker.modules()) {
        if (image.soname != "libc.so") continue;
        const std::uint32_t slot = image.bias + 0xa8c34;
        const std::uint32_t guard = image.bias + 0xc8cb0;
        std::uint32_t slot_value = 0, guard_value = 0;
        if (memory.accessible(slot, 4, dh2::kPageRead)) std::memcpy(&slot_value, memory.base() + slot, 4);
        if (memory.accessible(guard, 4, dh2::kPageRead)) std::memcpy(&guard_value, memory.base() + guard, 4);
        std::printf("  debug        : GOT[__stack_chk_guard]@0x%08x=0x%08x  expected=0x%08x  "
                    "*(guard)=0x%08x\n",
                    slot, slot_value, guard, guard_value);
    }

    // The initial thread's control block. Bionic reads errno at TP+0x29C and its canary at
    // [TP-4], so the thread pointer must be a real pthread_internal_t positioned at the end of
    // the static TLS block, and it has to exist before the first guest instruction.
    const std::uint32_t tls_total = linker.static_tls_size();
    // Two pages: the TLS block itself, and immediately above it the reserved flag word bionic's
    // interface uses to tell libc that the linker has set the process up.
    if (!memory.map_anon(kStackTop, 2 * dh2::kPageSize, PROT_READ | PROT_WRITE)) {
        std::fprintf(stderr, "dh2boot: cannot map the TLS control block\n");
        return 2;
    }
    const std::uint32_t tp = kStackTop + tls_total;

    // bionic's slot numbering on ARM32 is slot N at [TP + 4N]:
    //   slot 0 TLS_SLOT_SELF          -> &tls[0], i.e. TP itself
    //   slot 1 TLS_SLOT_THREAD_ID     -> the pthread_internal_t
    //   slot 2 TLS_SLOT_ERRNO         -> errno lives *in* the slot, so it stays zero
    //   slot 5 TLS_SLOT_STACK_GUARD   -> the canary the linker is expected to have written
    //                                  ("The linker ... filled in the main thread's TLS slot
    //                                   with that value", libc_init_dynamic.cpp)
    //
    // Slot 1 is the one that matters here. It must point at a *separate* pthread_internal_t,
    // not at the TLS block: bionic's main-thread registration writes the thread-list pointers
    // into that structure, and while slot 1 pointed at TP those two words landed on tls[0] and
    // tls[1] and wiped them -- after which __get_thread() read zero and the next dereference
    // faulted at 0x8.
    const std::uint32_t thread = kStackTop + dh2::kPageSize;
    const std::uint32_t self = tp;
    const std::uint32_t zero = 0;
    memory.copy_in(tp + 4 * 0, &self, 4);
    memory.copy_in(tp + 4 * 1, &thread, 4);
    memory.copy_in(tp + 4 * 2, &zero, 4);
    memory.copy_in(tp + 0x29C, &zero, 4);
    // [TP-4] is a second, independent path to the same thread structure. libc.so's
    // __libc_preinit_impl ends with
    //     mrc  p15,0,r0,c13,c0,3     ; r0 = TP
    //     ldr  r0,[r0,#-4]           ; r0 = *(TP-4)
    //     strb r5,[r0,#0xb49]        ; ((char*)r0)[0xb49] = 1
    // so leaving the word below the thread pointer zero makes that store land at 0xb49.
    memory.copy_in(tp - 4, &thread, 4);

    std::uint32_t canary = 0;
    if (getrandom(&canary, sizeof canary, 0) != static_cast<ssize_t>(sizeof canary)) {
        canary = static_cast<std::uint32_t>(tp) ^ 0x5bf03635u;
    }
    if (canary == 0) canary = 0x5bf03635u;
    memory.copy_in(tp + 4 * 5, &canary, 4);
    cpu.cp15().set_tpidruro(tp);
    cpu.watch(tp, 0x20);
    memory.watch(tp, 0x20);

    // The pthread_internal_t itself: thread list pointers clear, a tid, main-thread flag.
    const std::uint32_t main_pid = 1 + (static_cast<std::uint32_t>(::getpid()) % 30000u);
    memory.copy_in(thread + 0x00, &zero, 4);
    memory.copy_in(thread + 0x04, &zero, 4);
    memory.copy_in(thread + 0x08, &main_pid, 4);
    {
        const std::uint32_t one = 1;
        memory.copy_in(thread + 0x0C, &one, 4);
    }

    std::printf("  thread       : static TLS %u bytes, TP=0x%08x [TP]=0x%08x [TP+4]=0x%08x "
                "(pthread_internal_t @0x%08x, tid=%u) stack_guard=0x%08x\n",
                tls_total, tp, self, thread, thread, main_pid, canary);

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

    dh2::JniBridge jni(memory, cpu, kDynLimit);
    if (!jni.install(error)) {
        std::fprintf(stderr, "dh2boot: %s\n", error.c_str());
        return 2;
    }

    dh2::SyscallLayer syscalls(memory, cpu, linker.modules().front());

    // The last executed PCs, so a fault can name the instruction that caused it rather than the
    // one the JIT happened to leave in R15.
    static std::uint32_t trace[64];
    static std::size_t trace_at = 0;

    const auto call = [&](std::uint32_t target, const std::uint32_t* args, std::size_t arg_count,
                          std::uint32_t& result, dh2::Stop& stop_out) -> bool {
        std::uint32_t* regs = cpu.jit().Regs().data();
        for (int i = 0; i < 13; ++i) regs[i] = 0;
        for (std::size_t i = 0; i < arg_count && i < 4; ++i) regs[i] = args[i];
        regs[13] = kStackTop - 0x1000;
        regs[14] = trampoline | 1u;
        regs[15] = target & ~1u;
        std::uint32_t cpsr = 0x10 | 0x80 | 0x40;
        if ((target & 1u) != 0) cpsr |= 0x20;
        cpu.jit().SetCpsr(cpsr);
        for (;;) {
            if (trace_steps) {
                trace[trace_at++ % 64] = regs[15];
            }
            const dh2::Stop stop = trace_steps ? cpu.step() : cpu.run();
            if (stop.kind == dh2::StopKind::Svc) {
                if (stop.svc == kReturnSwi) {
                    result = regs[0];
                    return true;
                }
                if (stop.svc == kSlingshotSwi) {
                    // The application entry bionic calls. Its three arguments are the whole point
                    // of this gate: argc, argv and envp as __libc_init computed them.
                    std::printf("  slingshot    : reached at pc 0x%08x argc=%u argv=0x%08x envp=0x%08x\n",
                                regs[15], regs[0], regs[1], regs[2]);
                    std::fflush(stdout);
                    stop_out = stop;
                    stop_out.kind = dh2::StopKind::Step;
                    return false;
                }
                if (stop.svc == 0x0f0005) {  // __ARM_NR_set_tls
                    cpu.cp15().set_tpidruro(regs[0]);
                    regs[0] = 0;
                    continue;
                }
                if (loader_if.handle_svc(stop.svc)) continue;
                if (jni.handle_svc(stop.svc)) continue;
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

    // Initializer order. bionic's own source is explicit that __libc_preinit (libc.so's
    // constructor number 1) "is called by the dynamic linker when libc.so is loaded. This happens
    // before any other initializer", so libc.so goes first; everything else follows in reverse
    // load order, which is dependencies-before-dependents and the engine last.
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < linker.modules().size(); ++i) order.push_back(i);
    if (deps_first) {
        std::reverse(order.begin(), order.end());
    } else if (!root_first) {
        std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            const bool libc_a = linker.modules()[a].soname == "libc.so";
            const bool libc_b = linker.modules()[b].soname == "libc.so";
            if (libc_a != libc_b) return libc_a;
            return a > b;
        });
    }

    for (std::size_t step = 0; step < order.size() && !halted; ++step) {
        const std::size_t m = order[step];
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
            const std::uint32_t no_args[4] = {0, 0, 0, 0};
            if (!call(initializers[i], no_args, 0, result, stop)) {
                std::printf("  stop         : %s [%zu] at 0x%08x (link-time 0x%08x) -- %s\n",
                            image.soname.c_str(), i, initializers[i], initializers[i] - image.bias,
                            stop.describe().c_str());
                if (trace_steps) {
                    std::printf("  trace        : last executed PCs (oldest first)\n");
                    for (std::size_t k = 0; k < 64; ++k) {
                        const std::uint32_t pc = trace[(trace_at + k) % 64];
                        if (pc == 0) continue;
                        std::printf("      0x%08x\n", pc);
                    }
                }
                {
                    const auto word = [&](std::uint32_t address) -> std::uint32_t {
                        std::uint32_t v = 0;
                        if (memory.accessible(address, 4, dh2::kPageRead)) {
                            std::memcpy(&v, memory.base() + address, 4);
                        }
                        return v;
                    };
                    const std::uint32_t now = cpu.thread_pointer();
                    std::printf("  tls block    : TP=0x%08x [TP]=0x%08x [TP+4]=0x%08x [TP+8]=0x%08x "
                                "[TP+0x14]=0x%08x\n",
                                now, word(now), word(now + 4), word(now + 8), word(now + 0x14));
                }
                {
                    const std::uint32_t* rr = cpu.jit().Regs().data();
                    std::printf("  stack        : from sp 0x%08x\n", rr[13]);
                    for (int i = 0; i < 48; ++i) {
                        const std::uint32_t a = rr[13] + static_cast<std::uint32_t>(i) * 4;
                        std::uint32_t w = 0;
                        if (!memory.accessible(a, 4, dh2::kPageRead)) break;
                        std::memcpy(&w, memory.base() + a, 4);
                        std::printf("      [sp+%3d] 0x%08x\n", i * 4, w);
                    }
                }
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

    // The engine's own entry, as the game's DEX drives it. The recovered Java says
    //   DungeonHunter2.nativeInit(int)       -- the Activity's engine start
    //   GameRenderer.nativeGameRenderer()    -- the GLSurfaceView renderer's constructor
    //   GameRenderer.nativeConfig()
    //   GameRenderer.nativeRender()          -- once per frame
    // and the static block loads the library, which is the JNI_OnLoad above. nativeInit is
    // static and takes one int, so it is the first call that needs nothing but a jclass.
    if (!halted) {
        const std::uint32_t native_init =
            linker.find_symbol("Java_com_gameloft_android_GAND_GloftD2SS_DungeonHunter2_nativeInit");
        if (native_init == 0) {
            std::printf("  nativeInit   : symbol not found\n");
        } else {
            // nativeInit's first act is
            //     ldr r5, [got]        ; r5 = &g_cached_env
            //     ldr r3, [r5]         ; r3 = the JNIEnv the engine cached
            //     mov r0, r3
            //     ldr r3, [r3]         ; r3 = env->functions
            //     ldr pc, [r3, #0x54]  ; call slot 21
            // so it uses an env the JNI plumbing is expected to have cached already. Nothing has,
            // because there is no ART. The host seeds that global with the JNIEnv it built: the
            // GOT entry at engine+0x99c52c holds the global's address after relocation, so this
            // reads the slot rather than guessing an offset.
            // nativeInit reads its JNIEnv through a global, and the access trace names the exact
            // chain: GOT slot engine+0x99952c -> 0xeffbc518 -> the env (zero). The GOT entry is
            // relocated by our linker, so this reads the address of the global rather than
            // guessing an offset. In the real process the engine's own JNI plumbing fills it;
            // there is no ART here, so the host does, and says so.
            if (const dh2::LoadedImage* engine = &linker.modules().front()) {
                const std::uint32_t got = engine->bias + 0x99952c;
                std::uint32_t global = 0;
                if (memory.accessible(got, 4, dh2::kPageRead)) {
                    std::memcpy(&global, memory.base() + got, 4);
                }
                const std::uint32_t env_value = jni.env();
                std::printf("  nativeInit   : cached-env chain GOT[0x%08x]=0x%08x -> global\n", got,
                            global);
                if (global != 0 && memory.accessible(global, 4, dh2::kPageWrite) &&
                    memory.copy_in(global, &env_value, 4)) {
                    std::printf("  nativeInit   : seeded the cached JNIEnv at 0x%08x = 0x%08x\n", global,
                                env_value);
                } else {
                    std::printf("  nativeInit   : cannot seed the cached JNIEnv at 0x%08x\n", global);
                }
            }
            std::printf("  nativeInit   : calling 0x%08x(clazz=0x%08x, 0)\n", native_init,
                        jni.env());
            std::fflush(stdout);
            const std::uint32_t args[4] = {jni.env(), 0, 0, 0};
            dh2::Stop stop;
            std::uint32_t result = 0;
            cpu.trace_accesses(true);
            const std::uint64_t before = cpu.instruction_count();
            if (call(native_init, args, 2, result, stop)) {
                std::printf("  nativeInit   : returned (r0=0x%08x) after %llu instruction(s)\n", result,
                            static_cast<unsigned long long>(cpu.instruction_count() - before));
            } else {
                const std::uint32_t* rr = cpu.jit().Regs().data();
                std::printf("  nativeInit   : %s\n", stop.describe().c_str());
                std::printf("      at pc 0x%08x lr=0x%08x r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x\n",
                            rr[15], rr[14], rr[0], rr[1], rr[2], rr[3]);
                std::printf("      after %llu instruction(s)\n",
                            static_cast<unsigned long long>(cpu.instruction_count() - before));
                cpu.dump_accesses(stdout);
            }
        }
    }

    // JNI_OnLoad is the engine's real entry: it is a Java library's native half, so this is what
    // ART calls after System.loadLibrary. The host presents the JavaVM and JNIEnv it expects.
    {
        const std::uint32_t on_load = linker.find_symbol("JNI_OnLoad");
        if (on_load == 0) {
            std::printf("  JNI_OnLoad   : not exported by the closure\n");
        } else {
            std::printf("  JNI          : JavaVM=0x%08x JNIEnv=0x%08x\n", jni.java_vm(), jni.env());
            std::printf("  JNI_OnLoad   : calling 0x%08x(vm=0x%08x, reserved=0)\n", on_load,
                        jni.java_vm());
            std::fflush(stdout);
            const std::uint32_t args[4] = {jni.java_vm(), 0, 0, 0};
            dh2::Stop stop;
            std::uint32_t result = 0;
            if (call(on_load, args, 2, result, stop)) {
                std::printf("  JNI_OnLoad   : returned 0x%08x%s\n", result,
                            result == 0x00010006u ? " (JNI_VERSION_1_6)" : "");
            } else {
                std::printf("  JNI_OnLoad   : %s\n", stop.describe().c_str());
                const std::uint32_t* rr = cpu.jit().Regs().data();
                std::printf("      at pc 0x%08x lr=0x%08x r0=0x%08x r1=0x%08x r2=0x%08x r3=0x%08x\n",
                            rr[15], rr[14], rr[0], rr[1], rr[2], rr[3]);
            }
        }
    }

    // __libc_init: bionic's launch function, called by the executable's crt after the dynamic
    // linker has finished. Our host is the linker *and* the crt, so it calls it directly with the
    // argument block it built. The slingshot is a probe stub: reaching it proves bionic parsed
    // argc/argv/envp out of our stack image.
    if (!halted) {
        const std::uint32_t libc_init = linker.find_symbol("__libc_init");
        if (libc_init == 0) {
            std::printf("  __libc_init  : not found in the closure\n");
        } else {
            const std::uint32_t probe = memory.find_free(dh2::kPageSize, kDynLimit);
            const std::uint32_t structors = probe == 0 ? 0 : 0;  // zeroed block, allocated below
            std::uint32_t block = 0;
            if (probe != 0 && memory.map_anon(probe, dh2::kPageSize, PROT_READ | PROT_WRITE)) {
                const std::uint32_t body[2] = {0xEF000080u, 0xE12FFF1Eu};  // svc #0x80 ; bx lr
                memory.copy_in(probe, body, sizeof body);
                memory.protect(probe, dh2::kPageSize, PROT_READ | PROT_EXEC);
                block = probe + 0x100;  // a zeroed structors_array_t: the init arrays have run
            }
            (void)structors;
            if (probe == 0 || block == 0) {
                std::printf("  __libc_init  : cannot build the probe\n");
            } else {
                std::printf("  __libc_init  : calling 0x%08x with raw_args=0x%08x slingshot=0x%08x\n",
                            libc_init, sp, probe);
                std::fflush(stdout);
                const std::uint32_t args[4] = {sp, 0, probe, block};
                dh2::Stop stop;
                std::uint32_t result = 0;
                if (!call(libc_init, args, 4, result, stop)) {
                    std::printf("  __libc_init  : %s\n", stop.describe().c_str());
                }
            }
        }
    }
    std::printf("  instructions : %llu\n", static_cast<unsigned long long>(cpu.instruction_count()));
    syscalls.print_census(stdout);
    loader_if.print_census(stdout);
    jni.print_census(stdout);
    if (!loader_if.events().empty()) {
        std::printf("  loader iface : %zu event(s)\n", loader_if.events().size());
        for (const std::string& event : loader_if.events()) std::printf("      %s\n", event.c_str());
    }
    std::fflush(stdout);
    return 0;
}
