#include "dh2/jni.hpp"

#include <sys/mman.h>

#include <cstring>

#include "dh2/cpu.hpp"

namespace dh2 {

namespace {

// Names for the slots this host has had to reason about, by JNI's fixed table order. Everything
// else is reported by index; a name is added only once the engine has been seen to call it, so
// the table stays evidence-driven.
struct NamedSlot {
    std::uint32_t index;
    const char* name;
};

constexpr NamedSlot kNamed[] = {
    {4, "GetVersion"},
    {6, "FindClass"},
    {10, "GetSuperclass"},
    {14, "ThrowNew"},
    {15, "ExceptionOccurred"},
    {17, "ExceptionClear"},
    {21, "NewGlobalRef"},
    {22, "DeleteGlobalRef"},
    {23, "DeleteLocalRef"},
    {31, "GetObjectClass"},
    {33, "GetMethodID"},
    {94, "GetFieldID"},
    {113, "GetStaticMethodID"},
    {167, "NewStringUTF"},
    {169, "GetStringUTFChars"},
    {171, "ReleaseStringUTFChars"},
    {215, "RegisterNatives"},
    {216, "UnregisterNatives"},
    {219, "GetJavaVM"},
};

}  // namespace

JniBridge::JniBridge(GuestMemory& memory, Cpu& cpu, std::uint32_t dyn_limit)
    : mem_(memory), cpu_(cpu), dyn_limit_(dyn_limit) {}

const char* JniBridge::slot_name(std::uint32_t index) const {
    for (const NamedSlot& named : kNamed) {
        if (named.index == index) return named.name;
    }
    return nullptr;
}

std::uint32_t JniBridge::write_table(std::uint32_t at, std::uint32_t svc_base, std::uint32_t slots) {
    for (std::uint32_t i = 0; i < slots; ++i) {
        const std::uint32_t address = stubs_ + (svc_base - kNativeSvcBase + i) * 8;
        if (!mem_.copy_in(at + i * 4, &address, 4)) return 0;
    }
    return at;
}

bool JniBridge::install(std::string& error) {
    vm_ = mem_.find_free(kPageSize, dyn_limit_);
    stubs_ = vm_ != 0 ? mem_.find_free(kPageSize, vm_) : 0;
    data_ = stubs_ != 0 ? mem_.find_free(kPageSize, stubs_) : 0;
    if (vm_ == 0 || stubs_ == 0 || data_ == 0) {
        error = "no guest address space for the JNI bridge";
        return false;
    }
    if (!mem_.map_anon(vm_, kPageSize, PROT_READ | PROT_WRITE) ||
        !mem_.map_anon(stubs_, kPageSize, PROT_READ | PROT_WRITE) ||
        !mem_.map_anon(data_, kPageSize, PROT_READ | PROT_WRITE)) {
        error = "cannot map the JNI bridge";
        return false;
    }
    std::memset(mem_.base() + vm_, 0, kPageSize);
    std::memset(mem_.base() + data_, 0, kPageSize);

    // Every stub: ARM "svc #imm" then "bx lr", so a JNI call lands in guest code the host answers.
    const std::uint32_t total = kNativeSlots + kInvokeSlots;
    for (std::uint32_t i = 0; i < total; ++i) {
        const std::uint32_t svc = 0xEF000000u | (kNativeSvcBase + i);
        const std::uint32_t bx = 0xE12FFF1Eu;
        const std::uint32_t body[2] = {svc, bx};
        if (!mem_.copy_in(stubs_ + i * 8, body, sizeof body)) {
            error = "cannot write a JNI stub";
            return false;
        }
    }
    if (!mem_.protect(stubs_, kPageSize, PROT_READ | PROT_EXEC)) {
        error = "cannot make the JNI stubs executable";
        return false;
    }

    native_table_ = data_;
    invoke_table_ = data_ + kNativeSlots * 4;
    env_struct_ = vm_ + 0x100;
    data_used_ = kNativeSlots * 4 + kInvokeSlots * 4;

    if (write_table(native_table_, kNativeSvcBase, kNativeSlots) == 0 ||
        write_table(invoke_table_, kInvokeSvcBase, kInvokeSlots) == 0) {
        error = "cannot build a JNI function table";
        return false;
    }
    // JavaVM and JNIEnv are both "a pointer to a function table" as their first member.
    if (!mem_.copy_in(vm_, &invoke_table_, 4) || !mem_.copy_in(env_struct_, &native_table_, 4)) {
        error = "cannot build the JavaVM/JNIEnv headers";
        return false;
    }
    return true;
}

std::uint32_t JniBridge::class_stub(const std::string& name) {
    // A real class registry needs a DEX, and there is none here. What the engine gets back is a
    // distinct, dereferenceable guest address per requested class, so a jclass it passes back is
    // never a dangling pointer; the request itself is counted under FindClass in the census.
    (void)name;
    const std::uint32_t slot = 0x800 + static_cast<std::uint32_t>(class_counter_) * 16;
    if (slot + 16 > kPageSize) return data_ + 0x800;
    ++class_counter_;
    return data_ + slot;
}

bool JniBridge::handle_svc(std::uint32_t swi) {
    std::uint32_t* regs = cpu_.jit().Regs().data();
    if (swi >= kNativeSvcBase && swi < kNativeSvcBase + kNativeSlots) {
        const std::uint32_t index = swi - kNativeSvcBase;
        ++native_calls_[index];
        switch (index) {
            case 4:  // GetVersion
                regs[0] = 0x00010006u;  // JNI_VERSION_1_6
                break;
            case 6:  // FindClass: a real class registry does not exist yet, so a distinct,
                   // non-null token is handed back and the call is recorded by name.
                regs[0] = class_stub("(class)");
                break;
            case 15:  // ExceptionOccurred
                regs[0] = 0;
                break;
            case 17:  // ExceptionClear
                regs[0] = 0;
                break;
            case 21:  // NewGlobalRef: pass the reference through
                break;
            case 23:  // DeleteLocalRef
            case 22:  // DeleteGlobalRef
                regs[0] = 0;
                break;
            case 215:  // RegisterNatives: accepted; the binding is recorded, not simulated
                regs[0] = 0;
                break;
            case 216:  // UnregisterNatives
                regs[0] = 0;
                break;
            case 219: {  // GetJavaVM
                const std::uint32_t out = regs[1];
                if (out != 0) mem_.copy_in(out, &vm_, 4);
                regs[0] = 0;
                break;
            }
            default:
                regs[0] = 0;  // null / JNI_ERR: named in the census, not silently wrong
                break;
        }
        return true;
    }
    if (swi >= kInvokeSvcBase && swi < kInvokeSvcBase + kInvokeSlots) {
        const std::uint32_t index = swi - kInvokeSvcBase;
        ++invoke_calls_[index];
        if (index == 6) {  // GetEnv(vm, void** env, jint version)
            const std::uint32_t out = regs[1];
            if (out != 0) mem_.copy_in(out, &env_struct_, 4);
            regs[0] = 0;  // JNI_OK
        } else if (index == 4 || index == 7) {  // AttachCurrentThread(AsDaemon)
            const std::uint32_t out = regs[1];
            if (out != 0) mem_.copy_in(out, &env_struct_, 4);
            regs[0] = 0;
        } else {
            regs[0] = 0;
        }
        return true;
    }
    return false;
}

void JniBridge::print_census(FILE* out) const {
    std::uint64_t native_total = 0, invoke_total = 0;
    for (std::uint64_t n : native_calls_) native_total += n;
    for (std::uint64_t n : invoke_calls_) invoke_total += n;
    std::fprintf(out, "  JNI          : %llu native-interface call(s), %llu invoke-interface call(s)\n",
                 static_cast<unsigned long long>(native_total),
                 static_cast<unsigned long long>(invoke_total));
    for (std::uint32_t i = 0; i < kNativeSlots; ++i) {
        if (native_calls_[i] == 0) continue;
        const char* name = slot_name(i);
        std::fprintf(out, "      [%3u] %-24s calls=%llu\n", i, name != nullptr ? name : "(unnamed)",
                     static_cast<unsigned long long>(native_calls_[i]));
    }
    for (std::uint32_t i = 0; i < kInvokeSlots; ++i) {
        if (invoke_calls_[i] == 0) continue;
        std::fprintf(out, "      invoke[%u] calls=%llu\n", i,
                     static_cast<unsigned long long>(invoke_calls_[i]));
    }
}

}  // namespace dh2
