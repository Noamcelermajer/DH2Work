#pragma once

// A read-only view of the host filesystem for the guest.
//
// The engine opens its own content by name: /data/tweaker/*.tweaker_xml, DebugSwitches.savegame,
// shaders.pak, /system/fonts/droidsans.ttf, gameswf_effects.bdae. Until now every one of those
// returned -ENOENT because there was no filesystem layer at all.
//
// The mapping is deliberately simple and read-only: a guest path is looked for under each root,
// first as an absolute path below that root, then as the path itself. Nothing is written, and a
// path that does not resolve is still reported by name.

#include <cstdint>
#include <string>
#include <vector>

namespace dh2 {

class Vfs {
public:
    void add_root(const std::string& directory) { roots_.push_back(directory); }
    const std::vector<std::string>& roots() const { return roots_; }

    // The host path for a guest path, or empty when no root has it.
    std::string resolve(const std::string& guest_path) const;

private:
    std::vector<std::string> roots_;
};

}  // namespace dh2
