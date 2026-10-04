#include "dh2/loader_if.hpp"

#include <sys/mman.h>

#include <cstdio>
#include <cstring>

#include "dh2/cpu.hpp"

namespace dh2 {

namespace {

// dynsym order, as the reference and the platform's own ld-android.so declare them.
const char* const kSlots[] = {
    "__loader_android_get_LD_LIBRARY_PATH",
    "__loader_android_set_application_target_sdk_version",
    "__loader_dladdr",
    "__loader_dl_unwind_find_exidx",
    "__loader_dlclose",
    "__loader_android_get_application_target_sdk_version",
    "__loader_cfi_fail",
    "__loader_android_dlopen_ext",
    "__loader_android_dlwarning",
    "__loader_android_init_anonymous_namespace",
    "__loader_dl_iterate_phdr",
    "__loader_shared_globals",
    "__loader_android_link_namespaces",
    "__loader_android_handle_signal",
    "__loader_dlsym",
    "__loader_dlvsym",
    "__loader_remove_thread_local_dtor",
    "__loader_android_set_16kb_appcompat_mode",
    "rtld_db_dlactivity",
    "__loader_android_create_namespace",
    "__loader_android_get_exported_namespace",
    "__loader_android_link_namespaces_all_libs",
    "__loader_android_update_LD_LIBRARY_PATH",
    "__loader_dlerror",
    "__loader_dlopen",
    "__loader_add_thread_local_dtor",
};
constexpr std::size_t kSlotCount = sizeof(kSlots) / sizeof(kSlots[0]);

constexpr std::uint32_t kSvcBase = 0x100;
constexpr std::uint32_t kGlobalsSize = 0x2000;   // includes the 4 KiB libc_globals at +0x600

}  // namespace

LoaderInterface::LoaderInterface(GuestMemory& memory, Cpu& cpu, std::uint32_t dyn_limit)
    : mem_(memory), cpu_(cpu), dyn_limit_(dyn_limit) {}

bool LoaderInterface::is_interface_symbol(const std::string& name) {
    for (std::size_t i = 0; i < kSlotCount; ++i) {
        if (name == kSlots[i]) return true;
    }
    return false;
}

void LoaderInterface::note(const std::string& text) {
    if (events_.size() < 64) events_.push_back(text);
}

std::uint32_t LoaderInterface::alloc(std::uint32_t size, std::uint32_t align) {
    const std::uint32_t aligned = (data_used_ + align - 1) / align * align;
    if (data_ == 0 || aligned + size > kPageSize) return 0;
    data_used_ = aligned + size;
    return data_ + aligned;
}

void LoaderInterface::write32(std::uint32_t address, std::uint32_t value) {
    if (address != 0) mem_.copy_in(address, &value, 4);
}

std::uint32_t LoaderInterface::read32(std::uint32_t address) const {
    std::uint32_t value = 0;
    if (address != 0) std::memcpy(&value, mem_.base() + address, 4);
    return value;
}

bool LoaderInterface::install(std::uint32_t argc, const std::vector<std::string>& argv,
                              std::uint32_t auxv, std::string& error) {
    data_ = mem_.find_free(kPageSize, dyn_limit_);
    stubs_ = mem_.find_free(kPageSize, data_ != 0 ? data_ : dyn_limit_);
    globals_ = mem_.find_free(kGlobalsSize, stubs_ != 0 ? stubs_ : dyn_limit_);
    if (data_ == 0 || stubs_ == 0 || globals_ == 0) {
        error = "no guest address space for the loader interface";
        return false;
    }
    if (!mem_.map_anon(data_, kPageSize, PROT_READ | PROT_WRITE) ||
        !mem_.map_anon(stubs_, kPageSize, PROT_READ | PROT_WRITE) ||
        !mem_.map_anon(globals_, kGlobalsSize, PROT_READ | PROT_WRITE)) {
        error = "cannot map the loader interface";
        return false;
    }
    auxv_ = auxv;
    data_used_ = 0;

    // The 26 stubs: ARM "svc #(0x100+i)" then "bx lr". ARM state keeps them state-independent,
    // because a caller reaches them through blx.
    for (std::size_t i = 0; i < kSlotCount; ++i) {
        const std::uint32_t svc = 0xEF000000u | (kSvcBase + static_cast<std::uint32_t>(i));
        const std::uint32_t bx = 0xE12FFF1Eu;
        const std::uint32_t body[2] = {svc, bx};
        if (!mem_.copy_in(stubs_ + static_cast<std::uint32_t>(i) * 8, body, sizeof body)) {
            error = "cannot write a loader-interface stub";
            return false;
        }
    }

    const auto place_string = [&](const std::string& text) -> std::uint32_t {
        const std::uint32_t at = alloc(static_cast<std::uint32_t>(text.size() + 1), 4);
        if (at != 0) mem_.copy_in(at, text.c_str(), text.size() + 1);
        return at;
    };
    library_path_ = place_string("/system/lib:/vendor/lib");
    dlerror_text_ = place_string("no error");
    dlactivity_word_ = alloc(4, 4);  // rtld_db_dlactivity is a bool, not a function
    write32(dlactivity_word_, 0);

    // libc_shared_globals: only the fields a reached path reads are filled, and the rest stays
    // zero. Offsets are the ones measured by scanning libc.so's own call sites.
    // The block is allocated and left zero apart from the two fields whose meaning is verified
    // against this host's own libc.so below. The earlier attempt also wrote +0x51C (profile flag)
    // and +0x520 (mmap threshold) from a documented offset table, and that table does not match
    // this sysroot's libc.so: getenv read the environment pointer from +0x520, found the
    // threshold 0x20000 we had written there, and faulted dereferencing it. Offsets are only
    // written here once this binary has been shown to use them.
    std::memset(mem_.base() + globals_, 0, kGlobalsSize);
    write32(globals_ + 0x410, argc);
    write32(globals_ + 0x414, auxv_);
    if (!argv.empty()) {
        const std::uint32_t name = place_string(argv.front());
        write32(globals_ + 0x554, name);
        write32(globals_ + 0x55C, name);
    }

    if (!mem_.protect(stubs_, kPageSize, PROT_READ | PROT_EXEC)) {
        error = "cannot make the loader-interface stubs executable";
        return false;
    }
    return true;
}

std::uint32_t LoaderInterface::stub_for(const std::string& name) const {
    // rtld_db_dlactivity is a variable; every other entry is a function.
    if (name == "rtld_db_dlactivity" && dlactivity_word_ != 0) return dlactivity_word_;
    for (std::size_t i = 0; i < kSlotCount; ++i) {
        if (name == kSlots[i]) return stubs_ + static_cast<std::uint32_t>(i) * 8;
    }
    return 0;
}

const char* const* LoaderInterface::slot_names() const { return kSlots; }

void LoaderInterface::print_census(FILE* out) const {
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < kSlotCount; ++i) total += slot_calls_[i];
    std::fprintf(out, "  loader iface : %llu call(s) across the __loader_* table\n",
                 static_cast<unsigned long long>(total));
    for (std::size_t i = 0; i < kSlotCount; ++i) {
        if (slot_calls_[i] == 0) continue;
        std::fprintf(out, "      %-56s #%zu calls=%llu\n", kSlots[i], i,
                     static_cast<unsigned long long>(slot_calls_[i]));
    }
}

bool LoaderInterface::handle_svc(std::uint32_t swi) {
    if (swi < kSvcBase || swi >= kSvcBase + kSlotCount) return false;
    const std::size_t index = swi - kSvcBase;
    ++slot_calls_[index];
    std::uint32_t* regs = cpu_.jit().Regs().data();

    switch (index) {
        case 0:  // android_get_LD_LIBRARY_PATH: must never be NULL
            regs[0] = library_path_;
            break;
        case 1:  // set_application_target_sdk_version(int)
            target_sdk_ = regs[0];
            regs[0] = 0;
            break;
        case 5:  // get_application_target_sdk_version
            regs[0] = target_sdk_;
            break;
        case 11:  // shared_globals: the whole point
            regs[0] = globals_;
            break;
        case 16:  // remove_thread_local_dtor(fn, arg): both words must match
            (void)dtors_;
            regs[0] = 0;  // nothing to remove
            break;
        case 17:  // set_16kb_appcompat_mode(bool)
            note(std::string("set_16kb_appcompat_mode(") + (regs[0] ? "true" : "false") + ")");
            regs[0] = 0;
            break;
        case 22:  // update_LD_LIBRARY_PATH(const char*)
            note("update_LD_LIBRARY_PATH");
            regs[0] = 0;
            break;
        case 23:  // dlerror: must never be NULL
            regs[0] = dlerror_text_;
            break;
        case 25:  // add_thread_local_dtor(fn, arg)
            if (dtors_ < 65536) ++dtors_;
            regs[0] = 0;
            break;
        case 6:  // cfi_fail: a hard failure, reported rather than answered
            note("__loader_cfi_fail was called");
            regs[0] = 0;
            break;
        default:  // the stubs: bionic's own failure value for that signature
            regs[0] = 0;
            break;
    }
    return true;
}

}  // namespace dh2
