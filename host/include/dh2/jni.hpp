#pragma once

// The synthetic JNI surface.
//
// The engine is a Java library's native half: it never has a main(), it has JNI_OnLoad and the
// Java_com_gameloft_android_GAND_GloftD2SS_* natives that the game's own DEX drives. There is no
// ART in this process, so the host has to present the two structures JNI is built on:
//
//   JavaVM  -- first member is a pointer to the JNIInvokeInterface table (5 slots)
//   JNIEnv  -- first member is a pointer to the JNINativeInterface table (233 slots)
//
// Both tables are guest memory full of ARM stubs, one SVC each, exactly like the loader
// interface: a JNI call from the engine lands in working guest code and the host answers from the
// SVC callback. Every slot is counted, so what the engine actually uses is measured rather than
// guessed.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "dh2/guest_memory.hpp"

namespace dh2 {

class Cpu;

class JniBridge {
public:
    static constexpr std::uint32_t kNativeSvcBase = 0x200;
    static constexpr std::uint32_t kNativeSlots = 233;
    static constexpr std::uint32_t kInvokeSvcBase = 0x300;
    static constexpr std::uint32_t kInvokeSlots = 8;

    JniBridge(GuestMemory& memory, Cpu& cpu, std::uint32_t dyn_limit);

    bool install(std::string& error);

    std::uint32_t java_vm() const { return vm_; }
    std::uint32_t env() const { return env_struct_; }
    // A guest word holding the JNIEnv pointer. Engine code that reads its cached env through an
    // indirection needs *its* slot to point at a word like this one.
    std::uint32_t env_holder();

    bool handle_svc(std::uint32_t swi);
    void print_census(FILE* out) const;
    const char* slot_name(std::uint32_t index) const;
    const std::vector<std::string>& requests() const { return requests_; }

private:
    std::uint32_t write_table(std::uint32_t at, std::uint32_t svc_base, std::uint32_t slots);
    std::uint32_t class_stub(const std::string& name);

    GuestMemory& mem_;
    Cpu& cpu_;
    std::uint32_t dyn_limit_;
    std::uint32_t vm_ = 0;            // JavaVM struct
    std::uint32_t env_struct_ = 0;    // JNIEnv struct
    std::uint32_t native_table_ = 0;
    std::uint32_t invoke_table_ = 0;
    std::uint32_t stubs_ = 0;
    std::uint32_t data_ = 0;
    std::uint32_t data_used_ = 0;
    std::uint64_t native_calls_[kNativeSlots] = {};
    std::uint64_t invoke_calls_[kInvokeSlots] = {};
    int class_counter_ = 0;
    // Every class and method the engine asked for, in order. This is the JNI surface as the
    // engine actually uses it, recorded rather than assumed.
    std::vector<std::string> requests_;
    void note(const std::string& text);
};

}  // namespace dh2
