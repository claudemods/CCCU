#pragma once
#include "util.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace cccu::distro {

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

// Same, from the text of an os-release file (a system that cannot be read from disk, such as a running
// container) and whether it boots with systemd.
Info detect_text(const std::string& os_release, bool systemd);

const char* family_name(Family family);

// The bases the installer is built for, one line each ("Arch family: Arch, CachyOS, ...").
std::vector<std::string> supported_list();

// Generic package names (incus, grub, sddm, ...) for a family's package manager.
std::vector<std::string> map_packages(Family family, const std::vector<std::string>& generic);

// Shell snippets run inside the host chroot.
std::string install_missing_script(Family family, const std::vector<std::string>& packages);  // skips installed ones
std::string initramfs_and_boot_script(Family family);                                        // initramfs + GRUB

// A system taken from a live ISO still has the live-boot machinery (archiso / casper / live-boot / dracut-live
// hooks) that looks for the ISO at boot. This removes it, so the initramfs boots from the drive.
std::string live_cleanup_script(Family family);

// The kernel package(s) for a family, for a system source that has no kernel in /boot.
std::vector<std::string> kernel_packages(Family family, const std::string& id);

}  // namespace cccu::distro
