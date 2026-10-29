#pragma once
#include "util.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace claudeincus::distro {

// What the installer knows how to drive. The installer needs systemd inside the tty host and inside
// the container (units, logind, the first-boot service), so only systemd-based systems qualify.
enum class Family { Arch, Debian, Fedora, Suse, Unsupported };

struct Info {
    Family family = Family::Unsupported;
    std::string id;       // ID= from os-release (arch, cachyos, ubuntu, fedora, ...)
    std::string name;     // PRETTY_NAME
    std::string reason;   // why it is unsupported (empty when supported)
};

// Reads <root>/etc/os-release and checks for systemd (<root>/sbin/init).
Info detect(const std::filesystem::path& root);

const char* family_name(Family family);

// The bases the installer is built for, one line each ("Arch family: Arch, CachyOS, ...").
std::vector<std::string> supported_list();

// Generic package names (incus, grub, sddm, ...) for a family's package manager.
std::vector<std::string> map_packages(Family family, const std::vector<std::string>& generic);

// Shell snippets run inside the host chroot.
std::string install_missing_script(Family family, const std::vector<std::string>& packages);  // skips installed ones
std::string initramfs_and_boot_script(Family family);                                        // initramfs + GRUB

}  // namespace claudeincus::distro
