#include "dh2/gl.hpp"

#include <sys/mman.h>

#include <cstring>

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

bool GlBridge::handle_svc(std::uint32_t swi, const GuestLinker& linker) {
    if (swi < kStubSvcBase) return false;
    const std::size_t index = swi - kStubSvcBase;
    if (index >= linker.stub_count()) return false;
    const std::string& name = linker.stub_name(index);
    ++calls_[name];
    std::uint32_t* regs = cpu_.jit().Regs().data();

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
    std::fprintf(out, "  GL           : %llu call(s) into %zu imported entry point(s) of %zu stubs\n",
                 static_cast<unsigned long long>(total), calls_.size(), 0);
    for (const auto& entry : calls_) {
        std::fprintf(out, "      %-44s calls=%llu\n", entry.first.c_str(),
                     static_cast<unsigned long long>(entry.second));
    }
}

}  // namespace dh2
