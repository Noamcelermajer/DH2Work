#include "dh2/gl.hpp"

#include <dlfcn.h>
#include <sys/mman.h>

#include <algorithm>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "dh2/cpu.hpp"
#include "dh2/linker.hpp"

namespace dh2 {

namespace {
// One page of strings. The values are the honest ones for this host: it is not a GL
// implementation, it is a table that answers the entry points the engine needs to explore.
constexpr const char* kVendor = "DH2Work";
constexpr const char* kRenderer = "DH2Work own host";
constexpr const char* kVersion = "OpenGL ES 2.0 DH2Work";
constexpr const char* kGlsl = "OpenGL ES GLSL ES 1.00";
constexpr const char* kExtensions =
    "GL_OES_byte_coordinates GL_OES_fixed_point GL_OES_single_precision "
    "GL_OES_read_format GL_OES_compressed_paletted_texture GL_OES_draw_texture "
    "GL_OES_matrix_palette GL_OES_point_size_array GL_OES_point_sprite "
    "GL_OES_query_matrix GL_OES_texture_cube_map GL_OES_texture_env_crossbar "
    "GL_OES_texture_mirrored_repeat GL_OES_blend_subtract GL_OES_blend_equation_separate "
    "GL_OES_blend_func_separate GL_OES_EGL_image GL_OES_framebuffer_object "
    "GL_OES_depth24 GL_OES_depth32 GL_OES_element_index_uint GL_OES_mapbuffer "
    "GL_OES_packed_depth_stencil GL_OES_rgb8_rgba8 GL_OES_stencil1 GL_OES_stencil4 "
    "GL_OES_stencil8 GL_OES_texture_3D GL_OES_texture_npot GL_OES_vertex_array_object "
    "GL_OES_EGL_image_external GL_EXT_texture_format_BGRA8888 GL_APPLE_texture_format_BGRA8888";

// GL constants the engine asks about.
constexpr std::uint32_t kGlVendor = 0x1F00;
constexpr std::uint32_t kGlRenderer = 0x1F01;
constexpr std::uint32_t kGlVersion = 0x1F02;
constexpr std::uint32_t kGlExtensions = 0x1F03;
constexpr std::uint32_t kGlShadingLanguageVersion = 0x8B8C;
}  // namespace

GlBridge::GlBridge(GuestMemory& memory, Cpu& cpu) : mem_(memory), cpu_(cpu) {}

std::uint32_t GlBridge::place(const std::string& text) {
    const std::uint32_t at = data_ + used_;
    const std::uint32_t size = static_cast<std::uint32_t>(text.size()) + 1;
    if (used_ + size > kPageSize) return 0;
    if (!mem_.copy_in(at, text.c_str(), size)) return 0;
    used_ += size;
    return at;
}

bool GlBridge::install(std::string& error) {
    data_ = mem_.find_free(kPageSize, 0xF0000000u);
    if (data_ == 0 || !mem_.map_anon(data_, kPageSize, PROT_READ | PROT_WRITE)) {
        error = "cannot map the GL string page";
        return false;
    }
    std::memset(mem_.base() + data_, 0, kPageSize);
    vendor_ = place(kVendor);
    renderer_ = place(kRenderer);
    version_ = place(kVersion);
    glsl_ = place(kGlsl);
    extensions_ = place(kExtensions);
    if (version_ == 0 || extensions_ == 0) {
        error = "cannot write the GL strings";
        return false;
    }
    return true;
}

std::uint32_t GlBridge::string_for(std::uint32_t name) {
    switch (name) {
        case kGlVendor: return vendor_;
        case kGlRenderer: return renderer_;
        case kGlVersion: return version_;
        case kGlExtensions: return extensions_;
        case kGlShadingLanguageVersion: return glsl_;
        default: return 0;
    }
}

namespace {
// GL enums this host has to understand to answer a query.
constexpr std::uint32_t kGlCompileStatus = 0x8B81;
constexpr std::uint32_t kGlLinkStatus = 0x8B82;
constexpr std::uint32_t kGlTrue = 1;
constexpr std::uint32_t kGlFramebufferComplete = 0x8CD5;
constexpr std::uint32_t kGlMaxTextureSize = 0x0D33;
constexpr std::uint32_t kGlMaxVertexAttribs = 0x8869;
constexpr std::uint32_t kGlMaxTextureImageUnits = 0x8872;
constexpr std::uint32_t kGlMaxVertexTextureImageUnits = 0x8B4C;
constexpr std::uint32_t kGlMaxCombinedTextureImageUnits = 0x8B4D;
constexpr std::uint32_t kGlMaxVaryingVectors = 0x8DFC;
constexpr std::uint32_t kGlMaxVertexUniformVectors = 0x8DFB;
constexpr std::uint32_t kGlMaxFragmentUniformVectors = 0x8DFD;
constexpr std::uint32_t kGlViewport = 0x0BA2;
constexpr std::uint32_t kGlNumCompressedTextureFormats = 0x86A3;

void store(GuestMemory& memory, std::uint32_t address, std::uint32_t value) {
    if (address != 0 && memory.accessible(address, 4, kPageWrite)) {
        memory.copy_in(address, &value, 4);
    }
}
}  // namespace

bool GlBridge::create_context(int width, int height) {
    egl_lib_ = ::dlopen("libEGL.so", RTLD_NOW | RTLD_GLOBAL);
    if (egl_lib_ == nullptr) {
        std::fprintf(stderr, "dh2: no libEGL: %s\n", ::dlerror());
        return false;
    }
    auto get_display = reinterpret_cast<void* (*)(void*)>(::dlsym(egl_lib_, "eglGetDisplay"));
    auto initialize = reinterpret_cast<int (*)(void*, int*, int*)>(::dlsym(egl_lib_, "eglInitialize"));
    auto choose_config =
        reinterpret_cast<int (*)(void*, const int*, void**, int, int*)>(::dlsym(egl_lib_, "eglChooseConfig"));
    auto create_context = reinterpret_cast<void* (*)(void*, void*, void*, const int*)>(
        ::dlsym(egl_lib_, "eglCreateContext"));
    auto create_pbuffer = reinterpret_cast<void* (*)(void*, void*, const int*)>(
        ::dlsym(egl_lib_, "eglCreatePbufferSurface"));
    auto make_current = reinterpret_cast<int (*)(void*, void*, void*, void*)>(
        ::dlsym(egl_lib_, "eglMakeCurrent"));
    if (get_display == nullptr || initialize == nullptr || choose_config == nullptr ||
        create_context == nullptr || create_pbuffer == nullptr || make_current == nullptr) {
        std::fprintf(stderr, "dh2: libEGL is missing an entry point\n");
        return false;
    }

    void* display = get_display(nullptr);  // EGL_DEFAULT_DISPLAY
    int major = 0;
    int minor = 0;
    if (display == nullptr || !initialize(display, &major, &minor)) {
        std::fprintf(stderr, "dh2: eglInitialize failed\n");
        return false;
    }
    // EGL_SURFACE_TYPE/EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE/EGL_OPENGL_ES2_BIT, EGL_NONE.
    const int config_attributes[] = {0x3033, 0x0001, 0x3040, 0x0004, 0x3038};
    void* config = nullptr;
    int configs = 0;
    if (!choose_config(display, config_attributes, &config, 1, &configs) || configs == 0) {
        std::fprintf(stderr, "dh2: no EGL config for a pbuffer\n");
        return false;
    }
    const int surface_attributes[] = {0x3057, width, 0x3056, height, 0x3038};
    void* surface = create_pbuffer(display, config, surface_attributes);
    if (surface == nullptr) {
        std::fprintf(stderr, "dh2: eglCreatePbufferSurface failed\n");
        return false;
    }
    const int context_attributes[] = {0x3098, 2, 0x3038};  // EGL_CONTEXT_CLIENT_VERSION = 2
    void* context = create_context(display, config, nullptr, context_attributes);
    if (context == nullptr || !make_current(display, surface, surface, context)) {
        std::fprintf(stderr, "dh2: eglMakeCurrent failed\n");
        return false;
    }
    capture_width_ = width;
    capture_height_ = height;
    std::printf("  EGL          : context %d.%d on a %dx%d pbuffer, GLES2, made current\n", major,
                minor, width, height);
    return true;
}

bool GlBridge::capture_frame(const std::string& path) {
    if (gles_lib_ == nullptr || capture_width_ == 0) return false;
    auto read_pixels = reinterpret_cast<void (*)(int, int, int, int, unsigned, unsigned, void*)>(
        ::dlsym(gles_lib_, "glReadPixels"));
    if (read_pixels == nullptr) return false;
    const int width = capture_width_;
    const int height = capture_height_;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    read_pixels(0, 0, width, height, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */,
                pixels.data());
    if (std::FILE* file = std::fopen(path.c_str(), "wb")) {
        std::fprintf(file, "P6\n%d %d\n255\n", width, height);
        // PPM rows run top to bottom; GL's colour buffer runs bottom to top.
        for (int y = height - 1; y >= 0; --y) {
            for (int x = 0; x < width; ++x) {
                const std::uint8_t* pixel = &pixels[(static_cast<std::size_t>(y) * width + x) * 4];
                std::fwrite(pixel, 1, 3, file);
            }
        }
        std::fclose(file);
        std::printf("  capture      : wrote %s (%dx%d)\n", path.c_str(), width, height);
        return true;
    }
    return false;
}

bool GlBridge::attach_real_gl(const GuestLinker& linker) {
    // The engine links against libGLESv2.so and libGLESv1_CM.so. On Android both are the platform's
    // real implementation -- inside the emulator that is SwiftShader through the emulator's
    // translator -- so loading them and forwarding the engine's calls is the difference between
    // "the renderer believes it drew something" and "it drew something".
    gles_lib_ = ::dlopen("libGLESv2.so", RTLD_NOW | RTLD_GLOBAL);
    void* gles2 = gles_lib_;
    void* gles1 = ::dlopen("libGLESv1_CM.so", RTLD_NOW | RTLD_GLOBAL);
    if (gles2 == nullptr && gles1 == nullptr) {
        std::fprintf(stderr, "dh2: no platform GLES: %s\n", ::dlerror());
        return false;
    }
    real_.assign(linker.stub_count(), nullptr);
    std::size_t found = 0;
    for (std::size_t i = 0; i < linker.stub_count(); ++i) {
        const std::string& name = linker.stub_name(i);
        void* fn = gles2 != nullptr ? ::dlsym(gles2, name.c_str()) : nullptr;
        if (fn == nullptr && gles1 != nullptr) fn = ::dlsym(gles1, name.c_str());
        real_[i] = fn;
        if (fn != nullptr) ++found;
    }
    std::printf("  GL           : %zu of %zu entry points resolved to the platform GLES\n", found,
                linker.stub_count());
    return found != 0;
}

std::uint32_t GlBridge::copy_string_out(const char* text) {
    if (text == nullptr) return 0;
    return place(text);
}

namespace {
// What each entry point takes, so a guest call can be forwarded to the platform's GLES:
//   e = enum/int/flag, p = pointer into guest memory, f = float
// The host and the guest do not share an address space for pointers, so every 'p' is rebased
// through the guest mapping rather than passed through.
const char* signature_of(const std::string& name) {
    struct Entry { const char* name; const char* signature; };
    static const Entry table[] = {
        {"glActiveTexture", "e"}, {"glAttachShader", "ee"}, {"glBindBuffer", "ee"},
        {"glBindFramebuffer", "ee"}, {"glBindRenderbuffer", "ee"}, {"glBindTexture", "ee"},
        {"glBlendEquation", "e"}, {"glBlendFunc", "ee"}, {"glBlendFuncSeparate", "eeee"},
        {"glBufferData", "eipi"}, {"glCheckFramebufferStatus", "e"}, {"glClear", "e"},
        {"glClearColor", "ffff"}, {"glClearDepthf", "f"}, {"glClearStencil", "e"},
        {"glColorMask", "eeee"}, {"glCompileShader", "e"}, {"glCreateProgram", ""},
        {"glCreateShader", "e"}, {"glCullFace", "e"}, {"glDeleteBuffers", "ep"},
        {"glDeleteFramebuffers", "ep"}, {"glDeleteProgram", "e"}, {"glDeleteRenderbuffers", "ep"},
        {"glDeleteShader", "e"}, {"glDeleteTextures", "ep"}, {"glDepthFunc", "e"},
        {"glDepthMask", "e"}, {"glDepthRangef", "ff"}, {"glDetachShader", "ee"},
        {"glDisable", "e"}, {"glDisableVertexAttribArray", "e"}, {"glDrawArrays", "eee"},
        {"glDrawElements", "eeep"}, {"glEnable", "e"}, {"glEnableVertexAttribArray", "e"},
        {"glFinish", ""}, {"glFlush", ""}, {"glFramebufferRenderbuffer", "eeee"},
        {"glFramebufferTexture2D", "eeeee"}, {"glFrontFace", "e"}, {"glGenBuffers", "ep"},
        {"glGenFramebuffers", "ep"}, {"glGenRenderbuffers", "ep"}, {"glGenTextures", "ep"},
        {"glGenerateMipmap", "e"}, {"glGetAttribLocation", "ep"}, {"glGetError", ""},
        {"glGetFloatv", "ep"}, {"glGetIntegerv", "ep"}, {"glGetProgramInfoLog", "eepp"},
        {"glGetProgramiv", "eep"}, {"glGetShaderInfoLog", "eepp"}, {"glGetShaderiv", "eep"},
        {"glGetString", "e"}, {"glGetUniformLocation", "ep"}, {"glHint", "ee"},
        {"glLineWidth", "f"}, {"glLinkProgram", "e"}, {"glPixelStorei", "ee"},
        {"glPolygonOffset", "ff"}, {"glReadPixels", "eeeeep"}, {"glRenderbufferStorage", "eeee"},
        {"glSampleCoverage", "fe"}, {"glScissor", "eeee"}, {"glShaderSource", "eepp"},
        {"glTexImage2D", "eeeeeeeep"}, {"glTexParameterf", "eef"}, {"glTexParameteri", "eee"},
        {"glUniform1f", "ef"}, {"glUniform1i", "ee"}, {"glUniform2f", "eff"},
        {"glUniform3f", "efff"}, {"glUniform4f", "effff"}, {"glUniformMatrix4fv", "eeep"},
        {"glUseProgram", "e"}, {"glVertexAttribPointer", "eeeeep"}, {"glViewport", "eeee"},
        {"glEGLImageTargetTexture2DOES", "ep"},
    };
    for (const Entry& entry : table) {
        if (name == entry.name) return entry.signature;
    }
    return nullptr;
}
}  // namespace

bool GlBridge::invoke_real(std::size_t index, const std::string& name, std::uint32_t* regs) {
    void* fn = real_[index];
    const char* signature = signature_of(name);
    if (signature == nullptr) return false;  // unknown: the synthetic table answers it instead

    const std::uint32_t sp = regs[13];
    const std::uint32_t* ext = cpu_.jit().ExtRegs().data();
    auto stack_word = [&](int k) -> std::uint32_t {
        const std::uint32_t at = sp + static_cast<std::uint32_t>(k) * 4;
        std::uint32_t value = 0;
        if (mem_.accessible(at, 4, kPageRead)) std::memcpy(&value, mem_.base() + at, 4);
        return value;
    };
    auto rebase = [&](std::uint32_t guest) -> void* {
        if (guest == 0) return nullptr;
        if (!mem_.accessible(guest, 1, kPageRead)) return nullptr;
        return mem_.base() + guest;
    };

    // Sixteen, not eight: glTexImage2D takes nine arguments, and an eight-element array was
    // overflowed by one -- which the host's own stack protector caught as "stack corruption
    // detected" and aborted on.
    std::uintptr_t args[16] = {};
    float floats[16] = {};
    int n_ints = 0;
    int n_floats = 0;
    for (const char* c = signature; *c != 0; ++c) {
        if (*c == 'f') {
            std::memcpy(&floats[n_floats], &ext[n_floats], 4);
            ++n_floats;
        } else {
            const std::uint32_t raw = n_ints < 4 ? regs[n_ints] : stack_word(n_ints - 4);
            args[n_ints] = *c == 'p' ? reinterpret_cast<std::uintptr_t>(rebase(raw)) : raw;
            ++n_ints;
        }
    }

    const std::string sig(signature);
    // glGetString returns a pointer into the *platform's* memory, which is meaningless to the
    // guest, so the string is copied into guest memory and that address is returned.
    if (name == "glGetString") {
        const auto call = reinterpret_cast<const char* (*)(std::uint32_t)>(fn);
        regs[0] = copy_string_out(call(static_cast<std::uint32_t>(args[0])));
        return true;
    }
    // glShaderSource takes an array of pointers, and those pointers are guest addresses; the array
    // has to be rewritten before the platform can read it.
    if (name == "glShaderSource") {
        const std::uint32_t count = static_cast<std::uint32_t>(args[1]);
        const char* const* strings = reinterpret_cast<const char* const*>(args[2]);
        const char* host_strings[32] = {};
        for (std::uint32_t k = 0; k < count && k < 32; ++k) {
            std::uint32_t guest = 0;
            if (strings != nullptr) std::memcpy(&guest, reinterpret_cast<const std::uint8_t*>(strings) + k * 4, 4);
            host_strings[k] = static_cast<const char*>(rebase(guest));
        }
        const auto call = reinterpret_cast<void (*)(std::uint32_t, std::uint32_t, const char* const*,
                                                    const int*)>(fn);
        call(static_cast<std::uint32_t>(args[0]), count, host_strings,
             reinterpret_cast<const int*>(args[3]));
        return true;
    }

    if (sig.empty()) {
        reinterpret_cast<void (*)()>(fn)();
        return true;
    }
    if (sig == "e") {
        regs[0] = reinterpret_cast<std::uint32_t (*)(std::uint32_t)>(fn)(
            static_cast<std::uint32_t>(args[0]));
        return true;
    }
    if (sig == "ee") {
        regs[0] = reinterpret_cast<std::uint32_t (*)(std::uint32_t, std::uint32_t)>(fn)(
            static_cast<std::uint32_t>(args[0]), static_cast<std::uint32_t>(args[1]));
        return true;
    }
    if (sig == "eee") {
        regs[0] = reinterpret_cast<std::uint32_t (*)(std::uint32_t, std::uint32_t, std::uint32_t)>(fn)(
            static_cast<std::uint32_t>(args[0]), static_cast<std::uint32_t>(args[1]),
            static_cast<std::uint32_t>(args[2]));
        return true;
    }
    if (sig == "eeee") {
        reinterpret_cast<void (*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t)>(fn)(
            static_cast<std::uint32_t>(args[0]), static_cast<std::uint32_t>(args[1]),
            static_cast<std::uint32_t>(args[2]), static_cast<std::uint32_t>(args[3]));
        return true;
    }
    if (sig == "ep") {
        regs[0] = reinterpret_cast<std::uint32_t (*)(std::uint32_t, void*)>(fn)(
            static_cast<std::uint32_t>(args[0]), reinterpret_cast<void*>(args[1]));
        return true;
    }
    if (sig == "eep") {
        regs[0] = reinterpret_cast<std::uint32_t (*)(std::uint32_t, std::uint32_t, void*)>(fn)(
            static_cast<std::uint32_t>(args[0]), static_cast<std::uint32_t>(args[1]),
            reinterpret_cast<void*>(args[2]));
        return true;
    }
    if (sig == "eepp") {
        reinterpret_cast<void (*)(std::uint32_t, std::uint32_t, void*, void*)>(fn)(
            static_cast<std::uint32_t>(args[0]), static_cast<std::uint32_t>(args[1]),
            reinterpret_cast<void*>(args[2]), reinterpret_cast<void*>(args[3]));
        return true;
    }
    if (sig == "eipi") {
        reinterpret_cast<void (*)(std::uint32_t, std::uint32_t, void*, std::uint32_t)>(fn)(
            static_cast<std::uint32_t>(args[0]), static_cast<std::uint32_t>(args[1]),
            reinterpret_cast<void*>(args[2]), static_cast<std::uint32_t>(args[3]));
        return true;
    }
    if (sig == "eeep") {
        reinterpret_cast<void (*)(std::uint32_t, std::uint32_t, std::uint32_t, void*)>(fn)(
            static_cast<std::uint32_t>(args[0]), static_cast<std::uint32_t>(args[1]),
            static_cast<std::uint32_t>(args[2]), reinterpret_cast<void*>(args[3]));
        return true;
    }
    if (sig == "eeeee") {
        reinterpret_cast<void (*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                  std::uint32_t)>(fn)(
            static_cast<std::uint32_t>(args[0]), static_cast<std::uint32_t>(args[1]),
            static_cast<std::uint32_t>(args[2]), static_cast<std::uint32_t>(args[3]),
            static_cast<std::uint32_t>(args[4]));
        return true;
    }
    if (sig == "eeeeep") {
        reinterpret_cast<void (*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                  std::uint32_t, void*)>(fn)(
            static_cast<std::uint32_t>(args[0]), static_cast<std::uint32_t>(args[1]),
            static_cast<std::uint32_t>(args[2]), static_cast<std::uint32_t>(args[3]),
            static_cast<std::uint32_t>(args[4]), reinterpret_cast<void*>(args[5]));
        return true;
    }
    if (sig == "eeeeeep") {
        reinterpret_cast<void (*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                  std::uint32_t, std::uint32_t, void*)>(fn)(
            static_cast<std::uint32_t>(args[0]), static_cast<std::uint32_t>(args[1]),
            static_cast<std::uint32_t>(args[2]), static_cast<std::uint32_t>(args[3]),
            static_cast<std::uint32_t>(args[4]), static_cast<std::uint32_t>(args[5]),
            reinterpret_cast<void*>(args[6]));
        return true;
    }
    if (sig == "eeeeeeeep") {
        reinterpret_cast<void (*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                  std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                  void*)>(fn)(
            static_cast<std::uint32_t>(args[0]), static_cast<std::uint32_t>(args[1]),
            static_cast<std::uint32_t>(args[2]), static_cast<std::uint32_t>(args[3]),
            static_cast<std::uint32_t>(args[4]), static_cast<std::uint32_t>(args[5]),
            static_cast<std::uint32_t>(args[6]), static_cast<std::uint32_t>(args[7]),
            reinterpret_cast<void*>(args[8]));
        return true;
    }
    if (sig == "f") {
        reinterpret_cast<void (*)(float)>(fn)(floats[0]);
        return true;
    }
    if (sig == "ff") {
        reinterpret_cast<void (*)(float, float)>(fn)(floats[0], floats[1]);
        return true;
    }
    if (sig == "ffff") {
        reinterpret_cast<void (*)(float, float, float, float)>(fn)(floats[0], floats[1], floats[2],
                                                                   floats[3]);
        return true;
    }
    if (sig == "fe") {
        reinterpret_cast<void (*)(float, std::uint32_t)>(fn)(floats[0],
                                                             static_cast<std::uint32_t>(args[1]));
        return true;
    }
    if (sig == "ef") {
        reinterpret_cast<void (*)(std::uint32_t, float)>(fn)(static_cast<std::uint32_t>(args[0]),
                                                             floats[0]);
        return true;
    }
    if (sig == "eef") {
        reinterpret_cast<void (*)(std::uint32_t, std::uint32_t, float)>(fn)(
            static_cast<std::uint32_t>(args[0]), static_cast<std::uint32_t>(args[1]), floats[0]);
        return true;
    }
    if (sig == "eff") {
        reinterpret_cast<void (*)(std::uint32_t, float, float)>(fn)(
            static_cast<std::uint32_t>(args[0]), floats[0], floats[1]);
        return true;
    }
    if (sig == "efff") {
        reinterpret_cast<void (*)(std::uint32_t, float, float, float)>(fn)(
            static_cast<std::uint32_t>(args[0]), floats[0], floats[1], floats[2]);
        return true;
    }
    if (sig == "effff") {
        reinterpret_cast<void (*)(std::uint32_t, float, float, float, float)>(fn)(
            static_cast<std::uint32_t>(args[0]), floats[0], floats[1], floats[2], floats[3]);
        return true;
    }
    return false;
}

bool GlBridge::handle_svc(std::uint32_t swi, const GuestLinker& linker) {
    if (swi < kStubSvcBase) return false;
    const std::size_t index = swi - kStubSvcBase;
    if (index >= linker.stub_count()) return false;
    const std::string& name = linker.stub_name(index);
    std::uint32_t* regs = cpu_.jit().Regs().data();

    // A real GLES is the better answer whenever one is present: the engine's call reaches the
    // platform instead of being satisfied by this host's table.
    if (index < real_.size() && real_[index] != nullptr && invoke_real(index, name, regs)) {
        ++real_calls_;
        ++real_by_name_[name];
        return true;
    }

    ++calls_[name];

    // Object creation. A renderer that gets zero back for glCreateShader or glCreateProgram
    // treats it as an error and gives up, so these hand out distinct non-zero names.
    if (name == "glCreateShader" || name == "glCreateProgram" || name == "glCreateShaderProgramv") {
        regs[0] = ++next_object_;
        return true;
    }
    if (name == "glGenBuffers" || name == "glGenTextures" || name == "glGenFramebuffers" ||
        name == "glGenRenderbuffers" || name == "glGenVertexArraysOES") {
        const std::uint32_t count = regs[0];
        const std::uint32_t array = regs[1];
        for (std::uint32_t i = 0; i < count && i < 64; ++i) {
            store(mem_, array + i * 4, ++next_object_);
        }
        regs[0] = 0;
        return true;
    }

    // Queries whose answers decide whether the renderer continues.
    if (name == "glGetShaderiv" || name == "glGetProgramiv") {
        const std::uint32_t pname = regs[1];
        // A shader or program that reports a failed compile or link is abandoned by the caller,
        // and this host has no compiler to report honestly about; the compile status is the one
        // value that has to be true for the engine to keep going.
        store(mem_, regs[2], pname == kGlCompileStatus || pname == kGlLinkStatus ? kGlTrue : 0);
        regs[0] = 0;
        return true;
    }
    if (name == "glCheckFramebufferStatus") {
        regs[0] = kGlFramebufferComplete;
        return true;
    }
    if (name == "glGetString") {
        regs[0] = string_for(regs[0]);
        return true;
    }
    if (name == "glGetError") {
        regs[0] = 0;  // GL_NO_ERROR
        return true;
    }
    if (name == "glGetUniformLocation") {
        // Non-negative means "found"; -1 would make the caller skip the uniform entirely.
        regs[0] = ++next_object_;
        return true;
    }
    if (name == "glGetAttribLocation") {
        regs[0] = 0;
        return true;
    }
    if (name == "glGetIntegerv") {
        const std::uint32_t pname = regs[0];
        std::uint32_t value = 0;
        switch (pname) {
            case kGlMaxTextureSize: value = 4096; break;
            case kGlMaxVertexAttribs: value = 16; break;
            case kGlMaxTextureImageUnits: value = 16; break;
            case kGlMaxVertexTextureImageUnits: value = 8; break;
            case kGlMaxCombinedTextureImageUnits: value = 32; break;
            case kGlMaxVaryingVectors: value = 15; break;
            case kGlMaxVertexUniformVectors: value = 256; break;
            case kGlMaxFragmentUniformVectors: value = 224; break;
            case kGlNumCompressedTextureFormats: value = 0; break;
            default: value = 0; break;
        }
        store(mem_, regs[1], value);
        regs[0] = 0;
        return true;
    }

    // Shader plumbing, and the rest: succeed and do nothing. Named and counted, so the next entry
    // point that turns out to matter is visible in the census.
    regs[0] = 0;
    return true;
}

void GlBridge::print_census(FILE* out) const {
    std::uint64_t total = 0;
    for (const auto& entry : calls_) total += entry.second;
    std::fprintf(out, "  GL           : %llu call(s) satisfied by the host's table, %llu forwarded to "
                      "the platform GLES\n",
                 static_cast<unsigned long long>(total), static_cast<unsigned long long>(real_calls_));
    if (real_calls_ != 0) {
        std::fprintf(out, "  GL (real)    : %zu entry point(s), top by call count\n", real_.size());
        std::vector<std::pair<std::uint64_t, std::string>> ordered;
        for (const auto& entry : real_by_name_) ordered.emplace_back(entry.second, entry.first);
        std::sort(ordered.begin(), ordered.end(), std::greater<>());
        for (std::size_t i = 0; i < ordered.size() && i < 18; ++i) {
            std::fprintf(out, "      %-44s calls=%llu\n", ordered[i].second.c_str(),
                         static_cast<unsigned long long>(ordered[i].first));
        }
    }
    for (const auto& entry : calls_) {
        std::fprintf(out, "      %-44s calls=%llu\n", entry.first.c_str(),
                     static_cast<unsigned long long>(entry.second));
    }
}

}  // namespace dh2
