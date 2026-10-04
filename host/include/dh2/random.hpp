#pragma once

// Random bytes, without requiring a libc entry point the target may not have.
//
// getrandom() is API 28 on Android and the host is built for android-24 by default, so the
// syscall is issued directly and a /dev/urandom fallback covers a kernel that refuses it.

#include <cstddef>

namespace dh2 {

void random_bytes(void* out, std::size_t length);

}  // namespace dh2
