#pragma once
#include "config.hpp"
#include "util.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cccu::bootstrap {

// A built-in base package list for the tty host system. The user only picks one; nobody types the list.
struct Preset {
    std::string id;                      // "arch", "debian", "ubuntu", "fedora"
    std::string title;                   // shown in the menu
    std::string tool;                    // what installs it: pacstrap, debootstrap, dnf
    std::vector<std::string> packages;   // what is installed (the kernel is part of the list)
};

const std::vector<Preset>& presets();
const Preset* find(std::string_view id);

// Installs a preset (plus the optional extra packages of o.host_packages_file) as a fresh system into `root`.
Result from_list(const Options& o, const std::filesystem::path& root);

// Mounts an ISO, finds its root file system (archiso, Debian live, Ubuntu casper, Fedora LiveOS, or the
// largest squashfs/erofs image as a fallback) and extracts it into `root`.
Result from_iso(const std::string& iso, const std::filesystem::path& root);

}  // namespace cccu::bootstrap
