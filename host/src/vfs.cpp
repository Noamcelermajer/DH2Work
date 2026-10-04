#include "dh2/vfs.hpp"

#include <unistd.h>

#include <fstream>

namespace dh2 {

namespace {
// The engine sometimes asks for a path with a marker prefix (it build a "#" -prefixed name in one
// place). Strip the leading marker characters before matching.
std::string clean(const std::string& path) {
    std::string result = path;
    while (!result.empty() && (result.front() == '#' || result.front() == '@')) result.erase(0, 1);
    return result;
}

bool is_regular(const std::string& path) {
    std::ifstream probe(path, std::ios::binary);
    return probe.good();
}
}  // namespace

std::string Vfs::resolve(const std::string& guest_path) const {
    const std::string path = clean(guest_path);
    if (path.empty()) return {};
    for (const std::string& root : roots_) {
        std::string candidate = root;
        if (candidate.back() != '/') candidate += '/';
        candidate += path.front() == '/' ? path.substr(1) : path;
        if (is_regular(candidate)) return candidate;
        if (is_regular(path)) return path;
    }
    return {};
}

}  // namespace dh2
