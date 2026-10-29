#include "drive.hpp"

#include "proc.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include <sys/stat.h>

namespace claudeincus::drive {
namespace fs = std::filesystem;

namespace {

bool is_block_device(const std::string& path) {
    struct stat st {};
    return ::stat(path.c_str(), &st) == 0 && S_ISBLK(st.st_mode);
}

// sda -> sda1, nvme0n1 -> nvme0n1p1
std::string partition_path(const std::string& disk, int number) {
    const bool needs_p = std::isdigit(static_cast<unsigned char>(disk.back())) != 0;
    return disk + (needs_p ? "p" : "") + std::to_string(number);
}

// Names of the disk(s) backing "/" so we never wipe the running system.
std::vector<std::string> root_disks() {
    std::string source = query({"findmnt", "-no", "SOURCE", "/"});
    if (const auto bracket = source.find('['); bracket != std::string::npos) source.erase(bracket);  // btrfs subvol
    return split_lines(query({"lsblk", "-nrso", "NAME", source}));
}

}  // namespace

std::vector<std::string> required_tools(const Options& options) {
    return {"parted", "mkfs.vfat", options.fs == "ext4" ? "mkfs.ext4" : "mkfs.btrfs"};
}

Result prepare(const Options& o, Layout& layout) {
    if (o.fs != "btrfs" && o.fs != "ext4") return fail("--fs must be btrfs or ext4");
    if (o.drive.rfind("/dev/", 0) != 0 || !is_block_device(o.drive)) {
        return fail(o.drive + " is not a block device (example: /dev/sda or /dev/nvme0n1)");
    }

    const std::string disk = fs::path(o.drive).filename().string();
    const auto system_disks = root_disks();
    if (std::find(system_disks.begin(), system_disks.end(), disk) != system_disks.end()) {
        return fail(o.drive + " holds the running system (/). Refusing to wipe it.");
    }
    if (!query({"lsblk", "-nro", "MOUNTPOINTS", o.drive}).empty()) {
        return fail(o.drive + " or one of its partitions is mounted or used as swap. Unmount it first.");
    }
    if (!query({"findmnt", "-n", kTarget}).empty()) {
        return fail(std::string(kTarget) + " is already a mount point; unmount it first.");
    }

    step("Target drive");
    RunOptions listing;
    listing.mutating = false;
    run({"lsblk", "-o", "NAME,SIZE,MODEL,FSTYPE,MOUNTPOINTS", o.drive}, listing);
    warn("ALL DATA on " + o.drive + " will be destroyed.");
    warn("Layout: " + partition_path(o.drive, 1) + " = EFI boot, " + partition_path(o.drive, 2) + " = system (" + o.fs + ")");
    if (!o.assume_yes && ask("Type the device path again to confirm (" + o.drive + ")") != o.drive) {
        return fail("Cancelled. Nothing was changed.");
    }

    step("Partitioning and formatting " + o.drive);
    layout.efi_part = partition_path(o.drive, 1);
    layout.root_part = partition_path(o.drive, 2);

    if (!run({"wipefs", "-a", o.drive}).ok()) return fail("wipefs failed");
    if (!run({"parted", "-s", o.drive, "mklabel", "gpt",
              "mkpart", "ESP", "fat32", "1MiB", "551MiB",
              "mkpart", "root", o.fs, "551MiB", "100%",
              "set", "1", "esp", "on"})
             .ok()) {
        return fail("parted failed");
    }
    run({"partprobe", o.drive});
    run({"udevadm", "settle"});
    if (!dry_run() && (!is_block_device(layout.efi_part) || !is_block_device(layout.root_part))) {
        return fail("The new partitions did not appear.");
    }

    if (!run({"mkfs.vfat", "-F32", "-n", "EFI", layout.efi_part}).ok()) return fail("mkfs.vfat failed");
    if (o.fs == "btrfs") {
        if (!run({"mkfs.btrfs", "-f", "-L", "ROOT", layout.root_part}).ok()) return fail("mkfs.btrfs failed");
        // Create the subvolumes on a plain mount, then remount each one where it belongs.
        const std::string target = kTarget;
        if (!run({"mount", layout.root_part, target}).ok()) return fail("could not mount the system partition");
        for (const auto& sv : kBtrfsSubvolumes) {
            if (!run({"btrfs", "subvolume", "create", target + "/" + sv.name}).ok()) {
                return fail(std::string("could not create subvolume ") + sv.name);
            }
        }
        if (!run({"umount", target}).ok()) return fail("could not unmount the system partition");
        for (const auto& sv : kBtrfsSubvolumes) {
            const std::string mount_point = target + sv.mount_point;
            if (!dry_run()) {
                std::error_code ec;
                fs::create_directories(mount_point, ec);
            }
            const std::string options = std::string("subvol=") + sv.name +
                                        (o.compression.empty() ? "" : ",compress=" + o.compression);
            if (!run({"mount", "-o", options, layout.root_part, mount_point}).ok()) {
                return fail("could not mount subvolume " + std::string(sv.name));
            }
        }
    } else {
        if (!run({"mkfs.ext4", "-F", "-L", "ROOT", layout.root_part}).ok()) return fail("mkfs.ext4 failed");
        if (!run({"mount", layout.root_part, kTarget}).ok()) return fail("could not mount the system partition");
    }
    return {};
}

Result mount_efi(const Layout& layout) {
    std::error_code ec;
    fs::create_directories(fs::path(kTarget) / "boot/efi", ec);
    if (!run({"mount", layout.efi_part, std::string(kTarget) + "/boot/efi"}).ok()) {
        return fail("could not mount the EFI partition");
    }
    return {};
}

void unmount() {
    RunOptions quiet;
    quiet.quiet = true;
    run({"umount", "-R", kTarget}, quiet);
}

}  // namespace claudeincus::drive
