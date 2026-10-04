#pragma once

// Minimal CP15 coprocessor for user-mode ARM32 guest code.
//
// Only what a Linux/Android userspace guest actually touches is served:
//
//   MRC p15, 0, Rt, c13, c0, 3   read  TPIDRURO  -- bionic's TLS base for the current thread
//   MCR p15, 0, Rt, c13, c0, 3   write TPIDRURO  -- what __ARM_NR_set_tls ends up doing
//   MRC/MCR p15, 0, Rt, c13, c0, 2  TPIDRURW     -- written by some crt/libc paths
//   CP15 barriers (c7, c10, {4,5} / c7, c5, 4)   -- the pre-ARMv7 way to emit DSB/DMB/ISB
//
// Everything else compiles to a coprocessor exception, which surfaces as a named stop rather
// than as an undefined-instruction surprise.

#include <array>
#include <cstdint>
#include <optional>

#include <dynarmic/interface/A32/coprocessor.h>

namespace dh2 {

class Cp15 final : public Dynarmic::A32::Coprocessor {
public:
    std::uint32_t tpidruro() const { return tpidruro_; }
    void set_tpidruro(std::uint32_t value) { tpidruro_ = value; }
    std::uint32_t tpidrurw() const { return tpidrurw_; }

    std::optional<Callback> CompileInternalOperation(bool two, unsigned opc1, Dynarmic::A32::CoprocReg CRd,
                                                     Dynarmic::A32::CoprocReg CRn, Dynarmic::A32::CoprocReg CRm,
                                                     unsigned opc2) override;
    CallbackOrAccessOneWord CompileSendOneWord(bool two, unsigned opc1, Dynarmic::A32::CoprocReg CRn,
                                               Dynarmic::A32::CoprocReg CRm, unsigned opc2) override;
    CallbackOrAccessTwoWords CompileSendTwoWords(bool two, unsigned opc, Dynarmic::A32::CoprocReg CRm) override;
    CallbackOrAccessOneWord CompileGetOneWord(bool two, unsigned opc1, Dynarmic::A32::CoprocReg CRn,
                                              Dynarmic::A32::CoprocReg CRm, unsigned opc2) override;
    CallbackOrAccessTwoWords CompileGetTwoWords(bool two, unsigned opc, Dynarmic::A32::CoprocReg CRm) override;
    std::optional<Callback> CompileLoadWords(bool two, bool long_transfer, Dynarmic::A32::CoprocReg CRd,
                                             std::optional<std::uint8_t> option) override;
    std::optional<Callback> CompileStoreWords(bool two, bool long_transfer, Dynarmic::A32::CoprocReg CRd,
                                              std::optional<std::uint8_t> option) override;

private:
    std::uint32_t tpidruro_ = 0;
    std::uint32_t tpidrurw_ = 0;
};

}  // namespace dh2
