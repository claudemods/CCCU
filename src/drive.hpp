#pragma once
#include "config.hpp"
#include "util.hpp"

#include <string>
#include <vector>

namespace claudeincus::drive {

inline constexpr const char* kTarget = "/mnt";

// btrfs layout (same subvolumes as the old installerlinux script), all mounted with zstd.
struct Subvolume {
    const char* name;
    const char* mount_point;
};
inline constexpr Subvolume kBtrfsSubvolumes[] = {
    {"@", "/"},          {"@home", "/home"},       {"@root", "/root"},      {"@srv", "/srv"},
    {"@cache", "/var/cache"}, {"@tmp", "/tmp"},    {"@log", "/var/log"},
};

struct Layout {
    std::string efi_part;   // partition 1: EFI system partition (FAT32)
    std::string root_part;  // partition 2: the tty host system (btrfs or ext4)
};

// Host tools needed to prepare the drive (parted, mkfs.*).
std::vector<std::string> required_tools(const Options& options);

// Wipes options.drive, creates GPT with EFI (p1) + root (p2), formats both and mounts the root
// at kTarget. Refuses the drive holding the running system, mounted drives and swap.
Result prepare(const Options& options, Layout& layout);

// Mounts the EFI partition at kTarget/boot/efi (after the system has been unpacked).
Result mount_efi(const Layout& layout);

// Unmounts everything below kTarget.
void unmount();

}  // namespace claudeincus::drive
