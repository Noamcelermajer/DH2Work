/* DH2Work -- semantic tests for the T32 ARMv8 acquire/release and CRC32 patch.
 * SPDX-License-Identifier: 0BSD
 *
 * The instruction bytes exercised here come from a real assembler: build_and_run.sh
 * assembles t32_armv8_seq.s with clang --target=thumbv8a, and gen_seq_inc.py writes the
 * raw bytes and the name/offset table into t32_armv8_seq.inc.
 *
 * Without arguments this binary executes every added instruction on an A32 JIT and checks
 * the architectural result. With --expect-undefined the same bytes are executed against a
 * pristine Dynarmic checkout and each one must raise Exception::UndefinedInstruction; that
 * is the A/B evidence that the patch is what makes the instructions work.
 */

#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include <dynarmic/interface/A32/a32.h>
#include <dynarmic/interface/exclusive_monitor.h>

#include "t32_armv8_seq.inc"

namespace {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

constexpr u32 kMemorySize = 1u << 20;
constexpr u32 kCodeBase = 0x1000;
constexpr u32 kDataBase = 0x8000;

int g_passes = 0;
int g_failures = 0;

std::string Fmt(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return std::string(buffer);
}

void Report(const char* mode, const char* name, bool ok, const std::string& detail = {}) {
    if (ok) {
        std::printf("T32-%s %s PASS\n", mode, name);
        ++g_passes;
    } else {
        std::printf("T32-%s %s FAIL: %s\n", mode, name, detail.c_str());
        ++g_failures;
    }
}

// Looks up an assembler-generated encoding. occurrence selects among identical mnemonics
// (the exclusive-store tests use a second LDAEX form as their reservation setup).
const T32SeqEntry* E(const char* name, unsigned occurrence = 0) {
    unsigned seen = 0;
    for (unsigned i = 0; i < kSeqCount; ++i) {
        if (std::strcmp(kSeqTable[i].name, name) == 0) {
            if (seen++ == occurrence) {
                return &kSeqTable[i];
            }
        }
    }
    std::fprintf(stderr, "test error: no encoding named '%s' (occurrence %u)\n", name, occurrence);
    std::exit(2);
}

struct FlatMemory {
    std::vector<u8> bytes = std::vector<u8>(kMemorySize, 0);

    template<typename T>
    T Read(u32 address) const {
        T value;
        std::memcpy(&value, bytes.data() + address, sizeof(T));
        return value;
    }

    template<typename T>
    void Write(u32 address, T value) {
        std::memcpy(bytes.data() + address, &value, sizeof(T));
    }
};

struct Telemetry {
    bool undefined_instruction = false;
    bool unpredictable = false;
    bool other_exception = false;
    bool interpreter_fallback = false;
    bool svc = false;
};

class TestCallbacks final : public Dynarmic::A32::UserCallbacks {
public:
    explicit TestCallbacks(FlatMemory& memory) : memory(memory) {}

    Telemetry telemetry;
    u64 ticks_left = 0;

    u8 MemoryRead8(u32 vaddr) override { return memory.bytes[vaddr]; }
    u16 MemoryRead16(u32 vaddr) override { return memory.Read<u16>(vaddr); }
    u32 MemoryRead32(u32 vaddr) override { return memory.Read<u32>(vaddr); }
    u64 MemoryRead64(u32 vaddr) override { return memory.Read<u64>(vaddr); }

    void MemoryWrite8(u32 vaddr, u8 value) override { memory.Write<u8>(vaddr, value); }
    void MemoryWrite16(u32 vaddr, u16 value) override { memory.Write<u16>(vaddr, value); }
    void MemoryWrite32(u32 vaddr, u32 value) override { memory.Write<u32>(vaddr, value); }
    void MemoryWrite64(u32 vaddr, u64 value) override { memory.Write<u64>(vaddr, value); }

    bool MemoryWriteExclusive8(u32 vaddr, u8 value, u8) override { memory.Write<u8>(vaddr, value); return true; }
    bool MemoryWriteExclusive16(u32 vaddr, u16 value, u16) override { memory.Write<u16>(vaddr, value); return true; }
    bool MemoryWriteExclusive32(u32 vaddr, u32 value, u32) override { memory.Write<u32>(vaddr, value); return true; }
    bool MemoryWriteExclusive64(u32 vaddr, u64 value, u64) override { memory.Write<u64>(vaddr, value); return true; }

    void InterpreterFallback(u32, size_t) override { telemetry.interpreter_fallback = true; }
    void CallSVC(u32) override { telemetry.svc = true; }
    void ExceptionRaised(u32, Dynarmic::A32::Exception exception) override {
        switch (exception) {
        case Dynarmic::A32::Exception::UndefinedInstruction:
            telemetry.undefined_instruction = true;
            break;
        case Dynarmic::A32::Exception::UnpredictableInstruction:
            telemetry.unpredictable = true;
            break;
        default:
            telemetry.other_exception = true;
            break;
        }
    }

    void AddTicks(u64 ticks) override {
        ticks_left = ticks > ticks_left ? 0 : ticks_left - ticks;
    }
    u64 GetTicksRemaining() override { return ticks_left; }

private:
    FlatMemory& memory;
};

struct Session {
    FlatMemory memory;
    TestCallbacks callbacks{memory};
    Dynarmic::ExclusiveMonitor monitor{1};
    Dynarmic::A32::Jit jit;

    Session() : jit(MakeConfig()) {}

    Dynarmic::A32::UserConfig MakeConfig() {
        Dynarmic::A32::UserConfig config;
        config.callbacks = &callbacks;
        config.global_monitor = &monitor;
        config.arch_version = Dynarmic::A32::ArchVersion::v8;
        config.code_cache_size = 16 * 1024 * 1024;
        return config;
    }

    // Copies the selected encodings to kCodeBase, then a Thumb-2 self-loop so the block has
    // a terminal. ticks_left = 1 makes the dispatcher return once that block has run.
    void LoadSequence(std::initializer_list<const T32SeqEntry*> entries) {
        u32 offset = 0;
        for (const T32SeqEntry* entry : entries) {
            std::memcpy(memory.bytes.data() + kCodeBase + offset, kSeqBlob + entry->offset, entry->size);
            offset += entry->size;
        }
        const u16 self_loop = 0xE7FE;  // b .
        memory.Write<u16>(kCodeBase + offset, self_loop);
        callbacks.ticks_left = 1;
    }

    void RunThumb() {
        jit.SetCpsr(0x00000030);  // Thumb, user mode
        jit.Regs()[15] = kCodeBase;
        jit.Run();
    }

    u32 Reg(unsigned index) const { return jit.Regs()[index]; }
    void SetReg(unsigned index, u32 value) { jit.Regs()[index] = value; }

    bool Clean(std::string& why) const {
        const Telemetry& t = callbacks.telemetry;
        if (t.undefined_instruction) { why = "UndefinedInstruction raised"; return false; }
        if (t.unpredictable) { why = "UnpredictableInstruction raised"; return false; }
        if (t.other_exception) { why = "unexpected exception raised"; return false; }
        if (t.interpreter_fallback) { why = "InterpreterFallback"; return false; }
        if (t.svc) { why = "CallSVC"; return false; }
        return true;
    }
};

// Reflected CRC-32 update used by the ARM CRC32 family. The self-check below pins this
// against the published check values for "123456789".
constexpr u32 kPolyIso = 0xEDB88320u;
constexpr u32 kPolyCastagnoli = 0x82F63B78u;

u32 RefCrcByte(u32 crc, u8 byte, u32 poly) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
        crc = (crc >> 1) ^ ((crc & 1u) ? poly : 0u);
    }
    return crc;
}

u32 RefCrc(u32 crc, u32 data, int bytes, u32 poly) {
    for (int i = 0; i < bytes; ++i) {
        crc = RefCrcByte(crc, static_cast<u8>(data >> (8 * i)), poly);
    }
    return crc;
}

bool ReferenceCrcSelfCheck() {
    const char kCheck[] = "123456789";
    u32 iso = 0xFFFFFFFFu;
    u32 castagnoli = 0xFFFFFFFFu;
    for (const char* p = kCheck; *p != '\0'; ++p) {
        iso = RefCrcByte(iso, static_cast<u8>(*p), kPolyIso);
        castagnoli = RefCrcByte(castagnoli, static_cast<u8>(*p), kPolyCastagnoli);
    }
    return (iso ^ 0xFFFFFFFFu) == 0xCBF43926u && (castagnoli ^ 0xFFFFFFFFu) == 0xE3069283u;
}

void TestLdab() {
    Session s;
    s.LoadSequence({E("ldab")});
    s.SetReg(7, kDataBase);
    s.memory.Write<u8>(kDataBase, 0xA5);
    s.RunThumb();
    std::string why;
    const bool ok = s.Clean(why) && s.Reg(5) == 0x000000A5u;
    if (!ok && why.empty()) why = Fmt("r5=%08X expected 000000A5", s.Reg(5));
    Report("INSN", "ldab", ok, why);
}

void TestLdah() {
    Session s;
    s.LoadSequence({E("ldah")});
    s.SetReg(7, kDataBase);
    s.memory.Write<u16>(kDataBase, 0xBEEF);
    s.RunThumb();
    std::string why;
    const bool ok = s.Clean(why) && s.Reg(5) == 0x0000BEEFu;
    if (!ok && why.empty()) why = Fmt("r5=%08X expected 0000BEEF", s.Reg(5));
    Report("INSN", "ldah", ok, why);
}

void TestLdaex() {
    Session s;
    s.LoadSequence({E("ldaex")});
    s.SetReg(7, kDataBase);
    s.memory.Write<u32>(kDataBase, 0x11223344u);
    s.RunThumb();
    std::string why;
    const bool ok = s.Clean(why) && s.Reg(5) == 0x11223344u;
    if (!ok && why.empty()) why = Fmt("r5=%08X expected 11223344", s.Reg(5));
    Report("INSN", "ldaex", ok, why);
}

void TestLdaexb() {
    Session s;
    s.LoadSequence({E("ldaexb")});
    s.SetReg(7, kDataBase);
    s.memory.Write<u8>(kDataBase, 0x5A);
    s.RunThumb();
    std::string why;
    const bool ok = s.Clean(why) && s.Reg(5) == 0x0000005Au;
    if (!ok && why.empty()) why = Fmt("r5=%08X expected 0000005A", s.Reg(5));
    Report("INSN", "ldaexb", ok, why);
}

void TestLdaexh() {
    Session s;
    s.LoadSequence({E("ldaexh")});
    s.SetReg(7, kDataBase);
    s.memory.Write<u16>(kDataBase, 0xABCD);
    s.RunThumb();
    std::string why;
    const bool ok = s.Clean(why) && s.Reg(5) == 0x0000ABCDu;
    if (!ok && why.empty()) why = Fmt("r5=%08X expected 0000ABCD", s.Reg(5));
    Report("INSN", "ldaexh", ok, why);
}

void TestLdaexd() {
    Session s;
    s.LoadSequence({E("ldaexd")});
    s.SetReg(7, kDataBase);
    s.memory.Write<u64>(kDataBase, 0x1122334455667788ull);
    s.RunThumb();
    std::string why;
    const bool ok = s.Clean(why) && s.Reg(5) == 0x55667788u && s.Reg(6) == 0x11223344u;
    if (!ok && why.empty()) why = Fmt("r5=%08X r6=%08X expected 55667788/11223344", s.Reg(5), s.Reg(6));
    Report("INSN", "ldaexd", ok, why);
}

void TestStlb() {
    Session s;
    s.LoadSequence({E("stlb")});
    s.SetReg(7, kDataBase);
    s.SetReg(5, 0x12345678u);
    s.RunThumb();
    std::string why;
    const bool ok = s.Clean(why) && s.memory.Read<u8>(kDataBase) == 0x78u;
    if (!ok && why.empty()) why = Fmt("mem8=%02X expected 78", s.memory.Read<u8>(kDataBase));
    Report("INSN", "stlb", ok, why);
}

void TestStlh() {
    Session s;
    s.LoadSequence({E("stlh")});
    s.SetReg(7, kDataBase);
    s.SetReg(5, 0x12345678u);
    s.RunThumb();
    std::string why;
    const bool ok = s.Clean(why) && s.memory.Read<u16>(kDataBase) == 0x5678u;
    if (!ok && why.empty()) why = Fmt("mem16=%04X expected 5678", s.memory.Read<u16>(kDataBase));
    Report("INSN", "stlh", ok, why);
}

void TestStlex() {
    Session s;
    s.LoadSequence({E("ldaex", 1), E("stlex")});
    s.SetReg(7, kDataBase);
    s.SetReg(5, 0xDEADBEEFu);
    s.RunThumb();
    std::string why;
    const bool data_ok = s.memory.Read<u32>(kDataBase) == 0xDEADBEEFu;
    const bool status_ok = s.Reg(9) == 0u;
    const bool ok = s.Clean(why) && data_ok && status_ok;
    if (!ok && why.empty()) why = Fmt("mem32=%08X r9=%08X expected DEADBEEF/status 0", s.memory.Read<u32>(kDataBase), s.Reg(9));
    Report("INSN", "stlex", ok, why);
}

void TestStlexb() {
    Session s;
    s.LoadSequence({E("ldaexb", 1), E("stlexb")});
    s.SetReg(7, kDataBase);
    s.SetReg(5, 0xAABBCCDDu);
    s.RunThumb();
    std::string why;
    const bool data_ok = s.memory.Read<u8>(kDataBase) == 0xDDu;
    const bool status_ok = s.Reg(9) == 0u;
    const bool ok = s.Clean(why) && data_ok && status_ok;
    if (!ok && why.empty()) why = Fmt("mem8=%02X r9=%08X expected DD/status 0", s.memory.Read<u8>(kDataBase), s.Reg(9));
    Report("INSN", "stlexb", ok, why);
}

void TestStlexh() {
    Session s;
    s.LoadSequence({E("ldaexh", 1), E("stlexh")});
    s.SetReg(7, kDataBase);
    s.SetReg(5, 0xAABBCCDDu);
    s.RunThumb();
    std::string why;
    const bool data_ok = s.memory.Read<u16>(kDataBase) == 0xCCDDu;
    const bool status_ok = s.Reg(9) == 0u;
    const bool ok = s.Clean(why) && data_ok && status_ok;
    if (!ok && why.empty()) why = Fmt("mem16=%04X r9=%08X expected CCDD/status 0", s.memory.Read<u16>(kDataBase), s.Reg(9));
    Report("INSN", "stlexh", ok, why);
}

void TestStlexd() {
    Session s;
    s.LoadSequence({E("ldaexd", 1), E("stlexd")});
    s.SetReg(7, kDataBase);
    s.SetReg(5, 0x55667788u);
    s.SetReg(6, 0x11223344u);
    s.RunThumb();
    std::string why;
    const bool data_ok = s.memory.Read<u64>(kDataBase) == 0x1122334455667788ull;
    const bool status_ok = s.Reg(9) == 0u;
    const bool ok = s.Clean(why) && data_ok && status_ok;
    if (!ok && why.empty()) why = Fmt("mem64=%016llX r9=%08X expected 1122334455667788/status 0",
                                      static_cast<unsigned long long>(s.memory.Read<u64>(kDataBase)), s.Reg(9));
    Report("INSN", "stlexd", ok, why);
}

void TestCrc(const char* name, unsigned bytes, u32 polynomial) {
    Session s;
    s.LoadSequence({E(name)});
    const u32 accumulator = 0x12345678u;
    const u32 data = 0xDEADBEEFu;
    s.SetReg(9, accumulator);
    s.SetReg(7, data);
    s.RunThumb();
    const u32 expected = RefCrc(accumulator, data, static_cast<int>(bytes), polynomial);
    std::string why;
    const bool ok = s.Clean(why) && s.Reg(5) == expected;
    if (!ok && why.empty()) why = Fmt("r5=%08X expected %08X", s.Reg(5), expected);
    Report("INSN", name, ok, why);
}

void ExpectUndefined(const char* name) {
    Session s;
    s.LoadSequence({E(name)});
    s.RunThumb();
    const Telemetry& t = s.callbacks.telemetry;
    const bool ok = t.undefined_instruction && !t.unpredictable && !t.other_exception && !t.interpreter_fallback;
    std::string why;
    if (!ok) {
        if (t.unpredictable) {
            why = "UnpredictableInstruction, not UndefinedInstruction";
        } else if (t.other_exception) {
            why = "unexpected exception";
        } else if (t.interpreter_fallback) {
            why = "InterpreterFallback";
        } else if (t.undefined_instruction) {
            why = "UndefinedInstruction plus another diagnostic";
        } else {
            why = "no exception raised";
        }
    }
    Report("AB", name, ok, why);
}

const char* const kInstructionNames[] = {
    "ldab", "ldah", "ldaex", "ldaexb", "ldaexh", "ldaexd",
    "stlb", "stlh", "stlex", "stlexb", "stlexh", "stlexd",
    "crc32b", "crc32h", "crc32w", "crc32cb", "crc32ch", "crc32cw",
};

void RunSemanticTests() {
    TestLdab();
    TestLdah();
    TestLdaex();
    TestLdaexb();
    TestLdaexh();
    TestLdaexd();
    TestStlb();
    TestStlh();
    TestStlex();
    TestStlexb();
    TestStlexh();
    TestStlexd();
    TestCrc("crc32b", 1, kPolyIso);
    TestCrc("crc32h", 2, kPolyIso);
    TestCrc("crc32w", 4, kPolyIso);
    TestCrc("crc32cb", 1, kPolyCastagnoli);
    TestCrc("crc32ch", 2, kPolyCastagnoli);
    TestCrc("crc32cw", 4, kPolyCastagnoli);
}

}  // namespace

int main(int argc, char** argv) {
    const bool expect_undefined = argc > 1 && std::string(argv[1]) == "--expect-undefined";

    if (!ReferenceCrcSelfCheck()) {
        std::printf("T32-SELFTEST crc-reference FAIL\n");
        return 3;
    }
    std::printf("T32-SELFTEST crc-reference PASS\n");

    if (expect_undefined) {
        for (const char* name : kInstructionNames) {
            ExpectUndefined(name);
        }
        std::printf("T32-SUMMARY mode=pristine pass=%d fail=%d\n", g_passes, g_failures);
    } else {
        RunSemanticTests();
        std::printf("T32-SUMMARY mode=patched pass=%d fail=%d\n", g_passes, g_failures);
    }

    return g_failures == 0 ? 0 : 1;
}
