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
    // The native table indexes the first kNativeSlots stubs, the invoke table the next
    // kInvokeSlots. Deriving the offset by subtracting the SVC bases put every invoke entry 256
    // stubs past the end of the block, i.e. into memory that was never written.
    const std::uint32_t first = svc_base == kNativeSvcBase ? 0 : kNativeSlots;
    for (std::uint32_t i = 0; i < slots; ++i) {
        const std::uint32_t address = stubs_ + (first + i) * 8;
        if (!mem_.copy_in(at + i * 4, &address, 4)) return 0;
    }
    return at;
}

bool JniBridge::install(std::string& error) {
    // One contiguous reservation, laid out explicitly. Mapping each piece with its own
    // find_free() only reserves one page, so growing one of them with MAP_FIXED silently
    // replaced its neighbours: the three-page data region wiped the JavaVM and stub pages and
    // the process died inside nativeInit.
    const std::uint32_t span = 4 * kPageSize;
    const std::uint32_t base = mem_.find_free(span, dyn_limit_);
    if (base == 0) {
        error = "no guest address space for the JNI bridge";
        return false;
    }
    if (!mem_.map_anon(base, span, PROT_READ | PROT_WRITE)) {
        error = "cannot map the JNI bridge";
        return false;
    }
    vm_ = base;                    // page 0: JavaVM, JNIEnv, function tables, class tokens
    stubs_ = base + kPageSize;     // page 1: the stubs the tables point at
    data_ = base + 2 * kPageSize;  // pages 2-3: jmethodID records
    std::memset(mem_.base() + base, 0, span);

    // Every stub: ARM "svc #imm" then "bx lr", so a JNI call lands in guest code the host answers.
    // The native stubs occupy the first kNativeSlots slots and the invoke stubs the next
    // kInvokeSlots, which is also how write_table() indexes them.
    const std::uint32_t svc_bases[2] = {kNativeSvcBase, kInvokeSvcBase};
    const std::uint32_t svc_counts[2] = {kNativeSlots, kInvokeSlots};
    std::uint32_t stub_index = 0;
    for (int table = 0; table < 2; ++table) {
        for (std::uint32_t i = 0; i < svc_counts[table]; ++i, ++stub_index) {
            const std::uint32_t svc = 0xEF000000u | (svc_bases[table] + i);
            const std::uint32_t bx = 0xE12FFF1Eu;
            const std::uint32_t body[2] = {svc, bx};
            if (!mem_.copy_in(stubs_ + stub_index * 8, body, sizeof body)) {
                error = "cannot write a JNI stub";
                return false;
            }
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

namespace {
// A NUL-terminated guest string, bounded so a bad pointer cannot walk forever.
std::string guest_string(GuestMemory& memory, std::uint32_t address) {
    if (address == 0) return "(null)";
    const std::uint8_t* p = memory.host_ptr(address, 1, kPageRead);
    if (p == nullptr) return "(unreadable)";
    std::uint32_t length = 0;
    while (length < 256 && p[length] != 0) ++length;
    return std::string(reinterpret_cast<const char*>(p), length);
}
}  // namespace

void JniBridge::note(const std::string& text) {
    if (requests_.size() < 128) requests_.push_back(text);
}

std::uint32_t JniBridge::method_id(const std::string& name, const std::string& signature) {
    if (methods_used_ >= 64) return 0;
    const std::uint32_t at = data_ + 0x1000 + methods_used_ * 64;
    std::memset(mem_.base() + at, 0, 64);
    std::memcpy(mem_.base() + at, name.c_str(), name.size() < 31 ? name.size() : 31);
    std::memcpy(mem_.base() + at + 32, signature.c_str(), signature.size() < 31 ? signature.size() : 31);
    ++methods_used_;
    return at;
}

std::string JniBridge::method_name_at(std::uint32_t id) const {
    const std::uint32_t first = data_ + 0x1000;
    if (id < first || id >= first + 64 * 64) return {};
    return std::string(reinterpret_cast<const char*>(mem_.base() + id));
}

std::uint32_t JniBridge::answer_for(const std::string& name) const {
    // The Java side the engine asks for, answered without a network or a store. Every value here
    // is a deliberate choice, and each one is named so the choice is reviewable.
    if (name == "unlockDemo") return 1;            // the full game, not the demo
    if (name == "isWifiAlive") return 1;
    if (name == "isSupportMM") return 1;           // multimedia support
    if (name == "Get_PhoneLanguage") return 1;     // English
    if (name == "Get_PhoneManufacturer") return 0;
    if (name == "Get_PhoneModel") return 0;
    // Everything else is a no-op or a "not happening": sendAppToBackground, OpenGLive, OpenIGP,
    // NotifyTrophy, Exit, openBrowser, lockDemo, DisableLaunchGame, IncreaseLaunchTimes, and all
    // ten Gameloft Live calls (no login, no purchase, no server message, no error, network not
    // ready).
    return 0;
}

std::uint32_t JniBridge::env_holder() {
    const std::uint32_t at = data_ + 0xC00;
    if (!mem_.copy_in(at, &env_struct_, 4)) return 0;
    return at;
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
            case 6: {  // FindClass(name)
                const std::string name = guest_string(mem_, regs[1]);
                note("FindClass(" + name + ")");
                regs[0] = class_stub(name);
                break;
            }
            case 33: {  // GetMethodID(clazz, name, sig)
                note("GetMethodID(" + guest_string(mem_, regs[2]) + ", " + guest_string(mem_, regs[3]) + ")");
                regs[0] = 0;
                break;
            }
            case 113: {  // GetStaticMethodID(clazz, name, sig)
                const std::string name = guest_string(mem_, regs[2]);
                const std::string sig = guest_string(mem_, regs[3]);
                note("GetStaticMethodID(" + name + ", " + sig + ")");
                regs[0] = method_id(name, sig);
                break;
            }
            default:
                if (index >= 114 && index <= 143) {  // CallStatic<Type>Method{,V,A}
                    const std::string name = method_name_at(regs[2]);
                    ++method_calls_[name.empty() ? "(unknown method id)" : name];
                    // Object-returning slots get null; everything else gets the answered value.
                    regs[0] = (index <= 116) ? 0u : answer_for(name);
                    break;
                }
                regs[0] = 0;
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
    if (!requests_.empty()) {
        std::fprintf(out, "  JNI requests : %zu, in call order\n", requests_.size());
        for (const std::string& request : requests_) {
            std::fprintf(out, "      %s\n", request.c_str());
        }
    }
    if (!method_calls_.empty()) {
        std::fprintf(out, "  JNI callbacks: %zu method(s) called back into Java\n", method_calls_.size());
        for (const auto& entry : method_calls_) {
            std::fprintf(out, "      %-28s calls=%llu\n", entry.first.c_str(),
                         static_cast<unsigned long long>(entry.second));
        }
    }
    for (std::uint32_t i = 0; i < kInvokeSlots; ++i) {
        if (invoke_calls_[i] == 0) continue;
        std::fprintf(out, "      invoke[%u] calls=%llu\n", i,
                     static_cast<unsigned long long>(invoke_calls_[i]));
    }
}

}  // namespace dh2
