#include "dh2/initial_stack.hpp"

#include <elf.h>
#include <sys/random.h>

#include <cstring>

namespace dh2 {

namespace {
constexpr std::uint32_t kMaxStackImage = 256 * 1024;
}

std::uint32_t build_initial_stack(GuestMemory& mem, std::uint32_t stack_top,
                                  const std::vector<std::string>& argv,
                                  const std::vector<std::string>& envp, std::vector<AuxVector> auxv,
                                  const std::string& execfn, std::uint32_t* auxv_address) {
    std::uint32_t cursor = stack_top;
    bool overflow = false;

    const auto push_bytes = [&](const void* src, std::size_t len) -> std::uint32_t {
        if (len > kMaxStackImage || stack_top - cursor + len > kMaxStackImage ||
            !mem.accessible(cursor - static_cast<std::uint32_t>(len), len, kPageWrite)) {
            overflow = true;
            return 0;
        }
        cursor -= static_cast<std::uint32_t>(len);
        std::memcpy(mem.base() + cursor, src, len);
        return cursor;
    };
    const auto push_string = [&](const std::string& s) { return push_bytes(s.c_str(), s.size() + 1); };

    const std::uint32_t execfn_addr = push_string(execfn);
    const std::uint32_t platform_addr = push_string("v8l");

    std::vector<std::uint32_t> env_addrs;
    env_addrs.reserve(envp.size());
    for (const auto& e : envp) env_addrs.push_back(push_string(e));

    std::vector<std::uint32_t> arg_addrs;
    arg_addrs.reserve(argv.size());
    for (const auto& a : argv) arg_addrs.push_back(push_string(a));

    std::uint8_t random_bytes[16];
    if (getrandom(random_bytes, sizeof random_bytes, 0) != static_cast<ssize_t>(sizeof random_bytes)) {
        for (std::size_t i = 0; i < sizeof random_bytes; ++i) {
            random_bytes[i] = static_cast<std::uint8_t>(i * 37 + 11);
        }
    }
    const std::uint32_t random_addr = push_bytes(random_bytes, sizeof random_bytes);
    if (overflow) return 0;

    auxv.push_back({AT_RANDOM, random_addr});
    auxv.push_back({AT_PLATFORM, platform_addr});
    auxv.push_back({AT_EXECFN, execfn_addr});
    auxv.push_back({AT_NULL, 0});

    std::vector<std::uint32_t> table;
    table.reserve(1 + arg_addrs.size() + 1 + env_addrs.size() + 1 + auxv.size() * 2);
    table.push_back(static_cast<std::uint32_t>(argv.size()));
    table.insert(table.end(), arg_addrs.begin(), arg_addrs.end());
    table.push_back(0);
    table.insert(table.end(), env_addrs.begin(), env_addrs.end());
    table.push_back(0);
    for (const auto& a : auxv) {
        table.push_back(a.type);
        table.push_back(a.value);
    }

    const std::uint32_t table_bytes = static_cast<std::uint32_t>(table.size() * sizeof(std::uint32_t));
    const std::uint32_t sp = (cursor - table_bytes) & ~static_cast<std::uint32_t>(15);
    if (stack_top - sp > kMaxStackImage || !mem.accessible(sp, table_bytes, kPageWrite)) return 0;
    std::memcpy(mem.base() + sp, table.data(), table_bytes);
    if (auxv_address != nullptr) {
        // argc, argv[argc], NULL, envp[envc], NULL, then the auxv pairs.
        *auxv_address = sp + static_cast<std::uint32_t>(1 + argv.size() + 1 + env_addrs.size() + 1) * 4;
    }
    return sp;
}

}  // namespace dh2
