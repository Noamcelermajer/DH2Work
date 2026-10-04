// dh2selftest -- host-side checks for the Track A components.
//
// Everything here runs without a game binary: a synthetic ARM ELF32 image is built in the test,
// loaded by our own loader, and executed by our own Dynarmic host. The guest program is a short
// ARM sequence whose machine code was produced by clang and verified with llvm-objdump; its
// expected result (the sum 1..10 stored at 0x4000) is asserted.
//
//   published encoding, clang -march=armv7-a (armv7a-linux-androideabi21-clang), llvm-objdump:
//     e3a00000  mov r0, #0
//     e3a01001  mov r1, #1
//     e0800001  add r0, r0, r1
//     e2811001  add r1, r1, #1
//     e351000b  cmp r1, #11
//     bafffffb  blt .-8
//     e3a02901  mov r2, #0x4000
//     e5820000  str r0, [r2]
//     e3a070f8  mov r7, #248        @ exit_group
//     e3a00000  mov r0, #0
//     ef000000  svc #0
#include <elf.h>
#include <sys/mman.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "dh2/cpu.hpp"
#include "dh2/loader.hpp"
#include "dh2/syscalls.hpp"

namespace {

int checks = 0;
int failures = 0;

void check(bool condition, const char* what) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL %s\n", what);
    } else {
        std::printf("ok   %s\n", what);
    }
}

// The first PT_LOAD starts at file offset 0 and covers the whole file, so the ELF header and
// program header table are part of the loaded image -- exactly as a real executable lays them out.
constexpr std::uint32_t kImageVaddr = 0x8000;
constexpr std::uint32_t kCodeOffset = 0x100;
constexpr std::uint32_t kEntry = kImageVaddr + kCodeOffset;
constexpr std::uint32_t kDataVaddr = 0x4000;
constexpr std::uint32_t kStackTop = 0x90000;

const std::uint32_t kProgram[] = {
    0xe3a00000,  // mov r0, #0
    0xe3a01001,  // mov r1, #1
    0xe0800001,  // add r0, r0, r1
    0xe2811001,  // add r1, r1, #1
    0xe351000b,  // cmp r1, #11
    0xbafffffb,  // blt .-8
    0xe3a02901,  // mov r2, #0x4000
    0xe5820000,  // str r0, [r2]
    0xe3a070f8,  // mov r7, #248  (exit_group)
    0xe3a00000,  // mov r0, #0
    0xef000000,  // svc #0
};
constexpr std::uint32_t kProgramBytes = sizeof(kProgram);

// Build a minimal little-endian ARM ELF32 ET_EXEC with two PT_LOAD segments: the program at
// kCodeVaddr (R-X) and a zeroed data page at kDataVaddr (RW).
std::vector<std::uint8_t> make_synthetic_elf() {
    constexpr std::uint32_t kHeaderSize = sizeof(Elf32_Ehdr);
    constexpr std::uint32_t kCodeOffset = 0x100;
    constexpr std::uint32_t kDataOffset = 0x200;
    constexpr std::uint32_t kTotal = 0x300;

    std::vector<std::uint8_t> file(kTotal, 0);

    Elf32_Ehdr eh{};
    std::memcpy(eh.e_ident, ELFMAG, SELFMAG);
    eh.e_ident[EI_CLASS] = ELFCLASS32;
    eh.e_ident[EI_DATA] = ELFDATA2LSB;
    eh.e_ident[EI_VERSION] = EV_CURRENT;
    eh.e_type = ET_EXEC;
    eh.e_machine = EM_ARM;
    eh.e_version = EV_CURRENT;
    eh.e_entry = kEntry;
    eh.e_phoff = kHeaderSize;
    eh.e_ehsize = kHeaderSize;
    eh.e_phentsize = sizeof(Elf32_Phdr);
    eh.e_phnum = 2;
    std::memcpy(file.data(), &eh, sizeof eh);

    Elf32_Phdr code{};
    code.p_type = PT_LOAD;
    code.p_offset = 0;
    code.p_vaddr = kImageVaddr;
    code.p_paddr = kImageVaddr;
    code.p_filesz = kTotal;
    code.p_memsz = 0x1000;
    code.p_flags = PF_R | PF_X;
    code.p_align = 0x1000;

    Elf32_Phdr data{};
    data.p_type = PT_LOAD;
    data.p_offset = kDataOffset;
    data.p_vaddr = kDataVaddr;
    data.p_paddr = kDataVaddr;
    data.p_filesz = 0;
    data.p_memsz = 0x1000;
    data.p_flags = PF_R | PF_W;
    data.p_align = 0x1000;

    std::memcpy(file.data() + kHeaderSize, &code, sizeof code);
    std::memcpy(file.data() + kHeaderSize + sizeof code, &data, sizeof data);
    std::memcpy(file.data() + kCodeOffset, kProgram, kProgramBytes);
    return file;
}

std::string write_temp_file(const std::vector<std::uint8_t>& bytes) {
    const std::string path = "/tmp/dh2selftest-image.elf";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return {};
    std::fwrite(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    return path;
}

void test_memory() {
    dh2::GuestMemory memory;
    check(memory.ok(), "guest address space is reserved");
    if (!memory.ok()) return;
    check(!memory.accessible(0x0, 4, dh2::kPageRead), "the zero page is not readable");
    check(!memory.accessible(0x1000, 4, dh2::kPageRead), "an unmapped page is not readable");

    check(memory.map_anon(0x20000, 0x2000, PROT_READ | PROT_WRITE), "map_anon maps a region");
    check(memory.accessible(0x20000, 0x2000, dh2::kPageWrite), "the mapped region is writable");
    check(!memory.accessible(0x20000, 0x3000, dh2::kPageWrite), "access past the region is refused");
    check(memory.host_ptr(0x21000, 4, dh2::kPageWrite) != nullptr, "host_ptr resolves inside");
    check(memory.host_ptr(0x24000, 4, dh2::kPageRead) == nullptr, "host_ptr refuses outside");

    check(memory.protect(0x20000, 0x2000, PROT_READ), "protect narrows the region");
    check(!memory.accessible(0x20000, 4, dh2::kPageWrite), "the narrowed region is no longer writable");
    check(memory.accessible(0x20000, 4, dh2::kPageRead), "the narrowed region is still readable");

    check(memory.unmap(0x20000, 0x2000), "unmap releases the region");
    check(!memory.accessible(0x20000, 4, dh2::kPageRead), "the unmapped region is gone");

    const std::uint32_t free = memory.find_free(0x1000, 0x100000);
    check(free >= dh2::kLowestAlloc && free < 0x100000, "find_free returns a usable address");
    check(memory.range_free(free, 0x1000), "find_free returns a free range");
}

void test_loader_and_execution() {
    const std::string path = write_temp_file(make_synthetic_elf());
    check(!path.empty(), "the synthetic ELF image is written");
    if (path.empty()) return;

    dh2::GuestMemory memory;
    dh2::LoadedImage image;
    std::string error;
    const bool loaded = dh2::load_elf32(memory, path, 0xF0000000u, image, error);
    check(loaded, "load_elf32 loads the synthetic image");
    if (!loaded) {
        std::printf("     loader said: %s\n", error.c_str());
        return;
    }
    check(image.entry == kEntry, "the entry point is the program's entry");
    check(image.phdr == kImageVaddr + 52, "the program header table address is derived");
    check(image.phnum == 2, "both program headers are visible");
    check(memory.accessible(kEntry, kProgramBytes, dh2::kPageExec),
          "the code segment is executable");
    check(memory.accessible(kDataVaddr, 0x1000, dh2::kPageWrite), "the data segment is writable");
    check(!memory.accessible(kEntry, 4, dh2::kPageWrite),
          "the code segment is not host-writable");

    check(memory.map_anon(kStackTop - 0x1000, 0x1000, PROT_READ | PROT_WRITE), "the guest stack is mapped");

    dh2::Cpu cpu(memory, 0, true, true);
    std::uint32_t* regs = cpu.jit().Regs().data();
    for (int i = 0; i < 16; ++i) regs[i] = 0;
    regs[13] = kStackTop;
    regs[15] = image.entry;
    cpu.jit().SetCpsr(0x10 | 0x80 | 0x40);

    dh2::SyscallLayer syscalls(memory, cpu, image);

    int exit_code = -1;
    bool exited = false;
    int guard = 0;
    for (;;) {
        const dh2::Stop stop = cpu.run();
        if (stop.kind == dh2::StopKind::Svc) {
            const dh2::SyscallResult result = syscalls.handle(stop.svc);
            if (result.kind == dh2::SyscallResult::Kind::Exit) {
                exit_code = result.exit_code;
                exited = true;
                break;
            }
            if (++guard > 1000) break;
            continue;
        }
        std::printf("     unexpected stop: %s\n", stop.describe().c_str());
        break;
    }

    check(exited, "the guest reached exit_group");
    check(exit_code == 0, "the guest exited with code 0");
    check(cpu.instruction_count() > 0, "the guest executed instructions");
    check(syscalls.handled() >= 1, "the syscall layer handled the exit");

    std::uint32_t sum = 0;
    std::memcpy(&sum, memory.host_ptr(kDataVaddr, 4, dh2::kPageRead), 4);
    check(sum == 55, "the guest computed 1+2+...+10 = 55 and stored it");
}

void test_fault_reporting() {
    dh2::GuestMemory memory;
    // A single ARM instruction that loads from an address we never mapped. The page-flag check
    // must turn that into a reported guest fault, not a host crash.
    const std::uint32_t program[] = {0xe3a00000, 0xe3a01000, 0xe5902000, 0xef000000};  // ldr r2,[r0]
    check(memory.map_anon(0x80000, 0x1000, PROT_READ | PROT_WRITE), "a page is mapped writable");
    check(memory.copy_in(0x80000, program, sizeof program), "the program is copied into it");
    check(memory.protect(0x80000, 0x1000, PROT_READ | PROT_EXEC), "the page becomes read-execute");

    dh2::Cpu cpu(memory, 0, true, true);
    std::uint32_t* regs = cpu.jit().Regs().data();
    for (int i = 0; i < 16; ++i) regs[i] = 0;
    regs[13] = 0x90000;
    regs[15] = 0x80000;
    cpu.jit().SetCpsr(0x10 | 0x80 | 0x40);
    memory.map_anon(0x90000, 0x1000, PROT_READ | PROT_WRITE);

    const dh2::Stop stop = cpu.run();
    check(stop.kind == dh2::StopKind::MemoryFault, "an unmapped guest read is reported as a fault");
    check(stop.fault_addr == 0x00000000 && !stop.fault_write, "the fault names the refused address");
}

}  // namespace

int main() {
    std::printf("dh2selftest: DH2Work own-host component checks\n");
    test_memory();
    test_loader_and_execution();
    test_fault_reporting();
    std::printf("dh2selftest: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
