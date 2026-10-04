#include "dh2/random.hpp"

#include <sys/syscall.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>

namespace dh2 {

void random_bytes(void* out, std::size_t length) {
    std::uint8_t* bytes = static_cast<std::uint8_t*>(out);
    std::size_t done = 0;

#if defined(SYS_getrandom)
    while (done < length) {
        const long got = ::syscall(SYS_getrandom, bytes + done, length - done, 0);
        if (got <= 0) break;
        done += static_cast<std::size_t>(got);
    }
#endif
    if (done < length) {
        if (std::FILE* file = std::fopen("/dev/urandom", "rb")) {
            done += std::fread(bytes + done, 1, length - done, file);
            std::fclose(file);
        }
    }
    if (done < length) {
        // Last resort: a deterministic fill. Better than leaving the caller with uninitialised
        // bytes, and the only consumer is a canary or a guest's own getrandom.
        for (std::size_t i = done; i < length; ++i) {
            bytes[i] = static_cast<std::uint8_t>(i * 37u + 11u);
        }
    }
}

}  // namespace dh2
