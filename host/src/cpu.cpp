#include "dh2/cpu.hpp"

#include <cstdio>
#include <cstring>
#include <exception>

namespace dh2 {

namespace {

template <typename T>
T load_le(const std::uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

template <typename T>
void store_le(std::uint8_t* p, T v) {
    std::memcpy(p, &v, sizeof v);
}

}  // namespace

std::string Stop::describe() const {
    char buffer[192];
    switch (kind) {
        case StopKind::None:
            return "running";
        case StopKind::Step:
            std::snprintf(buffer, sizeof buffer, "halted at pc 0x%08x", pc);
            break;
        case StopKind::Svc:
            std::snprintf(buffer, sizeof buffer, "svc #0x%x at pc 0x%08x", svc, pc);
            break;
        case StopKind::MemoryFault:
            std::snprintf(buffer, sizeof buffer, "%s of a guest address at 0x%08x (pc 0x%08x)",
                          fault_write ? "write" : "read", fault_addr, pc);
            break;
        case StopKind::Exception:
            std::snprintf(buffer, sizeof buffer, "guest exception %d at pc 0x%08x",
                          static_cast<int>(exception), pc);
            break;
        case StopKind::InterpreterFallback:
            std::snprintf(buffer, sizeof buffer, "interpreter fallback for %zu instruction(s) at pc 0x%08x",
                          fallback_instructions, pc);
            break;
    }
    return buffer;
}

Cpu::Cpu(GuestMemory& memory, std::size_t processor_id, bool precise_faults, bool count_instructions)
    : mem_(memory),
      cp15_(std::make_shared<Cp15>()),
      monitor_(std::make_unique<Dynarmic::ExclusiveMonitor>(1)) {
    Dynarmic::A32::UserConfig cfg;
    cfg.callbacks = this;
    cfg.global_monitor = monitor_.get();
    cfg.processor_id = processor_id;
    cfg.arch_version = Dynarmic::A32::ArchVersion::v8;
    cfg.coprocessors[15] = cp15_;
    cfg.define_unpredictable_behaviour = true;
    cfg.enable_cycle_counting = count_instructions;
    cfg.check_halt_on_memory_access = precise_faults;
    // Deliberately unset: fastmem_pointer/page_table would let the JIT read guest memory
    // directly. That is faster and it is why this project measured before enabling it -- a wild
    // guest pointer must stay a reportable guest fault, not a host crash.
    jit_ = std::make_unique<Dynarmic::A32::Jit>(cfg);
    tick_budget_ = count_instructions ? (1ull << 24) : 0;
}

Cpu::~Cpu() = default;

Stop Cpu::run() {
    pending_ = Stop{};
    for (;;) {
        Dynarmic::HaltReason reason{};
        try {
            reason = jit_->Run();
        } catch (const std::exception& failure) {
            std::fprintf(stderr, "dh2: the translator gave up at pc 0x%08x: %s\n", jit_->Regs()[15],
                         failure.what());
            pending_.kind = StopKind::Exception;
            pending_.exception = Dynarmic::A32::Exception::UndefinedInstruction;
            pending_.pc = jit_->Regs()[15];
            break;
        } catch (...) {
            pending_.kind = StopKind::Exception;
            pending_.pc = jit_->Regs()[15];
            break;
        }
        jit_->ClearHalt(kStopHalt | kFaultHalt);
        if (pending_.kind != StopKind::None) break;  // a callback asked to stop
        if (Dynarmic::Has(reason, kStopHalt)) {
            pending_.kind = StopKind::Step;
            break;
        }
        // Any other reason (cycle budget exhausted, code cache invalidation) just means we
        // continue running.
    }
    Stop stop = pending_;
    if (stop.pc == 0 && stop.kind != StopKind::MemoryFault) stop.pc = jit_->Regs()[15];
    return stop;
}

void Cpu::halt(Dynarmic::HaltReason reason) { jit_->HaltExecution(reason); }

bool Cpu::check_access(std::uint32_t vaddr, std::uint32_t len, std::uint8_t need, bool write) {
    if (mem_.accessible(vaddr, len, need)) return true;
    if (pending_.kind == StopKind::None) {
        pending_.kind = StopKind::MemoryFault;
        pending_.fault_addr = vaddr;
        pending_.fault_write = write;
        // PC is still on the accessing instruction: check_halt_on_memory_access returns before
        // advancing, so this is the exact instruction to disassemble.
        pending_.pc = jit_->Regs()[15];
    }
    jit_->HaltExecution(kFaultHalt);
    return false;
}

std::uint8_t Cpu::MemoryRead8(std::uint32_t vaddr) {
    return check_access(vaddr, 1, kPageRead, false) ? mem_.base()[vaddr] : 0;
}

std::uint16_t Cpu::MemoryRead16(std::uint32_t vaddr) {
    return check_access(vaddr, 2, kPageRead, false) ? load_le<std::uint16_t>(mem_.base() + vaddr) : 0;
}

std::uint32_t Cpu::MemoryRead32(std::uint32_t vaddr) {
    return check_access(vaddr, 4, kPageRead, false) ? load_le<std::uint32_t>(mem_.base() + vaddr) : 0;
}

std::uint64_t Cpu::MemoryRead64(std::uint32_t vaddr) {
    return check_access(vaddr, 8, kPageRead, false) ? load_le<std::uint64_t>(mem_.base() + vaddr) : 0;
}

void Cpu::MemoryWrite8(std::uint32_t vaddr, std::uint8_t value) {
    if (check_access(vaddr, 1, kPageWrite, true)) mem_.base()[vaddr] = value;
}

void Cpu::MemoryWrite16(std::uint32_t vaddr, std::uint16_t value) {
    if (check_access(vaddr, 2, kPageWrite, true)) store_le<std::uint16_t>(mem_.base() + vaddr, value);
}

void Cpu::MemoryWrite32(std::uint32_t vaddr, std::uint32_t value) {
    if (check_access(vaddr, 4, kPageWrite, true)) store_le<std::uint32_t>(mem_.base() + vaddr, value);
}

void Cpu::MemoryWrite64(std::uint32_t vaddr, std::uint64_t value) {
    if (check_access(vaddr, 8, kPageWrite, true)) store_le<std::uint64_t>(mem_.base() + vaddr, value);
}

bool Cpu::MemoryWriteExclusive8(std::uint32_t vaddr, std::uint8_t value, std::uint8_t expected) {
    if (!check_access(vaddr, 1, static_cast<std::uint8_t>(kPageRead | kPageWrite), true)) return false;
    if (mem_.base()[vaddr] != expected) return false;
    mem_.base()[vaddr] = value;
    return true;
}

bool Cpu::MemoryWriteExclusive16(std::uint32_t vaddr, std::uint16_t value, std::uint16_t expected) {
    if (!check_access(vaddr, 2, static_cast<std::uint8_t>(kPageRead | kPageWrite), true)) return false;
    if (load_le<std::uint16_t>(mem_.base() + vaddr) != expected) return false;
    store_le<std::uint16_t>(mem_.base() + vaddr, value);
    return true;
}

bool Cpu::MemoryWriteExclusive32(std::uint32_t vaddr, std::uint32_t value, std::uint32_t expected) {
    if (!check_access(vaddr, 4, static_cast<std::uint8_t>(kPageRead | kPageWrite), true)) return false;
    if (load_le<std::uint32_t>(mem_.base() + vaddr) != expected) return false;
    store_le<std::uint32_t>(mem_.base() + vaddr, value);
    return true;
}

bool Cpu::MemoryWriteExclusive64(std::uint32_t vaddr, std::uint64_t value, std::uint64_t expected) {
    if (!check_access(vaddr, 8, static_cast<std::uint8_t>(kPageRead | kPageWrite), true)) return false;
    if (load_le<std::uint64_t>(mem_.base() + vaddr) != expected) return false;
    store_le<std::uint64_t>(mem_.base() + vaddr, value);
    return true;
}

std::optional<std::uint32_t> Cpu::MemoryReadCode(std::uint32_t vaddr) {
    if (!mem_.accessible(vaddr, 4, kPageExec)) return std::nullopt;
    return load_le<std::uint32_t>(mem_.base() + vaddr);
}

void Cpu::InterpreterFallback(std::uint32_t pc, std::size_t num_instructions) {
    if (pending_.kind == StopKind::None) {
        pending_.kind = StopKind::InterpreterFallback;
        pending_.pc = pc;
        pending_.fallback_instructions = num_instructions;
    }
    halt();
}

void Cpu::CallSVC(std::uint32_t swi) {
    ++svcs_;
    if (pending_.kind == StopKind::None) {
        pending_.kind = StopKind::Svc;
        pending_.svc = swi;
        // The PC has already advanced past the SVC, which is exactly where a syscall return
        // belongs, so the driver simply resumes.
        pending_.pc = jit_->Regs()[15];
    }
    halt();
}

void Cpu::ExceptionRaised(std::uint32_t pc, Dynarmic::A32::Exception exception) {
    if (pending_.kind == StopKind::None) {
        pending_.kind = StopKind::Exception;
        pending_.exception = exception;
        pending_.pc = pc;
    }
    halt();
}

void Cpu::AddTicks(std::uint64_t ticks) { ticks_ += ticks; }

std::uint64_t Cpu::GetTicksRemaining() {
    // Never zero: zero would halt the run loop immediately. The counter is what we want, so the
    // budget is a constant and AddTicks accumulates executed instructions.
    return tick_budget_ != 0 ? tick_budget_ : 0xFFFFFFFFull;
}

}  // namespace dh2
