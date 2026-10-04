#pragma once

// The guest CPU: Dynarmic's A32 JIT driven by our own callbacks.
//
// Every guest memory access goes through the callbacks (no fastmem, no page table), so an
// access outside the loaded image is reported as a named guest fault with the guest address,
// the direction and the program counter -- not as a host SIGSEGV. That is a deliberate
// correctness-first default; the measured cost of the checked path is a separate question the
// roadmap already flags.

#include <cstdint>
#include <memory>
#include <string>

#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/A32/config.h>
#include <dynarmic/interface/exclusive_monitor.h>
#include <dynarmic/interface/halt_reason.h>

#include "dh2/cp15.hpp"
#include "dh2/guest_memory.hpp"

namespace dh2 {

inline constexpr Dynarmic::HaltReason kStopHalt = Dynarmic::HaltReason::UserDefined1;
// With check_halt_on_memory_access the JIT emits a test after every guest data access and
// returns with PC still on the accessing instruction, so a refused access is precise.
inline constexpr Dynarmic::HaltReason kFaultHalt = Dynarmic::HaltReason::MemoryAbort;

enum class StopKind {
    None,
    Step,                 // the run loop ran out of work / HaltExecution was called
    Svc,                  // the guest executed SVC
    MemoryFault,          // a guest data access was refused by the page flags
    Exception,            // undefined instruction, coprocessor exception, ...
    InterpreterFallback,  // the backend asked the interpreter to step (unsupported here)
};

struct Stop {
    StopKind kind = StopKind::None;
    std::uint32_t pc = 0;
    std::uint32_t svc = 0;
    std::uint32_t fault_addr = 0;
    bool fault_write = false;
    Dynarmic::A32::Exception exception = Dynarmic::A32::Exception::UndefinedInstruction;
    std::size_t fallback_instructions = 0;

    std::string describe() const;
};

class Cpu final : public Dynarmic::A32::UserCallbacks {
public:
    // count_instructions turns Dynarmic's cycle counter into an executed-instruction count by
    // charging exactly one tick per instruction; the evidence this project reports is expressed
    // in instructions, so the default is on.
    Cpu(GuestMemory& memory, std::size_t processor_id, bool precise_faults, bool count_instructions);
    ~Cpu() override;

    Dynarmic::A32::Jit& jit() { return *jit_; }
    Cp15& cp15() { return *cp15_; }

    Stop run();
    void halt(Dynarmic::HaltReason reason = kStopHalt);

    std::uint64_t instruction_count() const { return ticks_; }
    std::uint64_t svc_count() const { return svcs_; }
    std::uint32_t thread_pointer() const { return cp15_->tpidruro(); }

    // Dynarmic::A32::UserCallbacks
    std::uint8_t MemoryRead8(std::uint32_t vaddr) override;
    std::uint16_t MemoryRead16(std::uint32_t vaddr) override;
    std::uint32_t MemoryRead32(std::uint32_t vaddr) override;
    std::uint64_t MemoryRead64(std::uint32_t vaddr) override;
    void MemoryWrite8(std::uint32_t vaddr, std::uint8_t value) override;
    void MemoryWrite16(std::uint32_t vaddr, std::uint16_t value) override;
    void MemoryWrite32(std::uint32_t vaddr, std::uint32_t value) override;
    void MemoryWrite64(std::uint32_t vaddr, std::uint64_t value) override;
    bool MemoryWriteExclusive8(std::uint32_t vaddr, std::uint8_t value, std::uint8_t expected) override;
    bool MemoryWriteExclusive16(std::uint32_t vaddr, std::uint16_t value, std::uint16_t expected) override;
    bool MemoryWriteExclusive32(std::uint32_t vaddr, std::uint32_t value, std::uint32_t expected) override;
    bool MemoryWriteExclusive64(std::uint32_t vaddr, std::uint64_t value, std::uint64_t expected) override;
    std::optional<std::uint32_t> MemoryReadCode(std::uint32_t vaddr) override;
    void InterpreterFallback(std::uint32_t pc, std::size_t num_instructions) override;
    void CallSVC(std::uint32_t swi) override;
    void ExceptionRaised(std::uint32_t pc, Dynarmic::A32::Exception exception) override;
    void AddTicks(std::uint64_t ticks) override;
    std::uint64_t GetTicksRemaining() override;

private:
    bool check_access(std::uint32_t vaddr, std::uint32_t len, std::uint8_t need, bool write);

    GuestMemory& mem_;
    std::shared_ptr<Cp15> cp15_;
    // LDREX/STREX (and the LDAEX/STLEX forms in the ARMv8 family) go through this monitor. The
    // JIT asserts if an exclusive instruction is translated without one.
    std::unique_ptr<Dynarmic::ExclusiveMonitor> monitor_;
    std::unique_ptr<Dynarmic::A32::Jit> jit_;
    Stop pending_;
    std::uint64_t ticks_ = 0;
    std::uint64_t svcs_ = 0;
    std::uint64_t tick_budget_ = 0;
};

}  // namespace dh2
