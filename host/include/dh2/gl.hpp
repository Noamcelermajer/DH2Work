#pragma once

// The GL entry points the engine imports.
//
// The engine imports 92 of them through its own GOT, and until now every one was a "return 0"
// stub -- which is what made glitch::video::CCommonGLDriver::genericDriverInit stop on the first
// frame: it calls glGetString(GL_VERSION) and immediately scans the result.
//
// The host answers by name from the SVC the generated stub carries. Only the entry points the
// engine is actually seen to use get a real answer; the rest still return 0 and are counted.

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>

#include "dh2/guest_memory.hpp"

namespace dh2 {

class Cpu;
class GuestLinker;

class GlBridge {
public:
    GlBridge(GuestMemory& memory, Cpu& cpu);

    // The strings glGetString returns, written into guest memory once.
    bool install(std::string& error);

    // Connect the entry points to the platform's own GLES, when there is one. Inside an Android
    // emulator (or on a device) that is a real GL implementation, and the engine's calls should
    // reach it rather than being answered by this host's table.
    bool attach_real_gl(const GuestLinker& linker);
    bool real_gl() const { return !real_.empty(); }

    // The platform's GLES does nothing without a current EGL context on this thread, so one is
    // created against a pbuffer surface. Declared through dlsym rather than the EGL headers: the
    // host must build on a target that has no EGL at all.
    bool create_context(int width, int height);

    // Read the colour buffer back and write it as a PPM, which is the evidence that something was
    // actually drawn rather than merely called.
    bool capture_frame(const std::string& path);

    bool handle_svc(std::uint32_t swi, const GuestLinker& linker);
    void print_census(FILE* out) const;

private:
    std::uint32_t string_for(std::uint32_t name);

    GuestMemory& mem_;
    Cpu& cpu_;
    std::uint32_t data_ = 0;
    std::uint32_t used_ = 0;
    std::map<std::string, std::uint64_t> calls_;
    std::uint32_t vendor_ = 0, renderer_ = 0, version_ = 0, extensions_ = 0, glsl_ = 0;
    // GL object names have to be distinct and non-zero; the engine abandons a shader or program
    // whose name it reads as zero.
    std::uint32_t next_object_ = 1;

    std::uint32_t place(const std::string& text);
    bool invoke_real(std::size_t index, const std::string& name, std::uint32_t* regs);
    std::uint32_t copy_string_out(const char* text);

    std::vector<void*> real_;
    void* gles_lib_ = nullptr;
    void* egl_lib_ = nullptr;
    int capture_width_ = 0;
    int capture_height_ = 0;
    std::uint64_t real_calls_ = 0;
    std::map<std::string, std::uint64_t> real_by_name_;
    std::uint32_t scratch_ = 0;
    std::uint32_t scratch_used_ = 0;
};

}  // namespace dh2
