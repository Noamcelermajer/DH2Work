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

bool GlBridge::handle_svc(std::uint32_t swi, const GuestLinker& linker) {
    if (swi < kStubSvcBase) return false;
    const std::size_t index = swi - kStubSvcBase;
    if (index >= linker.stub_count()) return false;
    const std::string& name = linker.stub_name(index);
    ++calls_[name];
    std::uint32_t* regs = cpu_.jit().Regs().data();

    if (name == "glGetString") {
        regs[0] = string_for(regs[0]);
    } else if (name == "glGetError") {
        regs[0] = 0;  // GL_NO_ERROR
    } else if (name == "glGetIntegerv") {
        // Two integers is the common case (a query with a single result). Writing the whole buffer
        // would need the real count, so exactly what the engine asks for is written and the rest
        // is left alone.
        if (regs[1] != 0 && mem_.accessible(regs[1], 8, kPageWrite)) {
            const std::uint32_t zero = 0;
            mem_.copy_in(regs[1], &zero, 4);
        }
        regs[0] = 0;
    } else {
        // Every other entry point: the value a GL implementation returns for a call that cannot
        // do anything here. Named and counted, so the next one to matter is visible.
        regs[0] = 0;
    }
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
