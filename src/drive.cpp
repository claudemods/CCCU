#include "drive.hpp"

#include "proc.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

#include <sys/stat.h>

namespace cccu::drive {
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

// Names of the disk(s) backing "/" so we never wipe the running system. Partitions are included
// (lsblk -s lists the device and everything below it, e.g. nvme0n1p4 and nvme0n1).
std::vector<std::string> root_disks() {
    std::string source = query({"findmnt", "-no", "SOURCE", "/"});
    if (const auto bracket = source.find('['); bracket != std::string::npos) source.erase(bracket);  // btrfs subvol
    return split_lines(query({"lsblk", "-nrso", "NAME", source}));
}

std::string lsblk_field(const std::string& device, const char* field) { return trim(query({"lsblk", "-ndo", field, device})); }

// Formats the partitions of the layout and mounts the root at kTarget. `format_boot` false = keep the boot partition as it is.
Result format_and_mount(const Options& o, const Layout& layout, bool format_boot) {
    // wipefs on the disk only removes the disk's own signatures. What was INSIDE the old partitions (an ext4
    // superblock, say) is still there at the same offsets and survives mkfs.btrfs: mount then picks the old ext4
    // and "btrfs subvolume create" fails with "Inappropriate ioctl for device". Wipe the new partitions first.
    if (format_boot) run({"wipefs", "-a", layout.efi_part});
    run({"wipefs", "-a", layout.root_part});

    if (format_boot && !run({"mkfs.vfat", "-F32", "-n", "EFI", layout.efi_part}).ok()) return fail("mkfs.vfat failed");
    if (o.fs == "btrfs") {
        if (!run({"mkfs.btrfs", "-f", "-L", "ROOT", layout.root_part}).ok()) return fail("mkfs.btrfs failed");
        // Create the subvolumes on a plain mount, then remount each one where it belongs.
        const std::string target = kTarget;
        if (!run({"mount", "-t", "btrfs", layout.root_part, target}).ok()) return fail("could not mount the system partition as btrfs");
        if (!dry_run() && query({"findmnt", "-no", "FSTYPE", target}) != "btrfs") {
            return fail(layout.root_part + " is not mounted as btrfs (an old filesystem signature is still on it)");
        }
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
            const std::string options = std::string("subvol=") + sv.name + btrfs_compress_options(o.compression);
            if (!run({"mount", "-t", "btrfs", "-o", options, layout.root_part, mount_point}).ok()) {
                return fail("could not mount subvolume " + std::string(sv.name));
            }
        }
    } else {
        if (!run({"mkfs.ext4", "-F", "-L", "ROOT", layout.root_part}).ok()) return fail("mkfs.ext4 failed");
        if (!run({"mount", "-t", "ext4", layout.root_part, kTarget}).ok()) return fail("could not mount the system partition as ext4");
    }
    return {};
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
    return format_and_mount(o, layout, true);
}

Result prepare_partitions(const Options& o, Layout& layout) {
    if (o.fs != "btrfs" && o.fs != "ext4") return fail("--fs must be btrfs or ext4");
    if (o.boot_part.empty() || o.root_part.empty()) return fail("Give both partitions: --boot-part <dev> and --root-part <dev>.");
    if (o.boot_part == o.root_part) return fail("The boot and root partitions must be two different partitions.");

    for (const std::string* part : {&o.boot_part, &o.root_part}) {
        if (part->rfind("/dev/", 0) != 0 || !is_block_device(*part)) {
            return fail(*part + " is not a block device (example: /dev/nvme0n1p4 or /dev/sda4)");
        }
        if (lsblk_field(*part, "TYPE") != "part") return fail(*part + " is not a partition (this mode uses existing partitions; use --drive to wipe a whole drive).");
        if (!query({"lsblk", "-nro", "MOUNTPOINTS", *part}).empty()) {
            return fail(*part + " is mounted or used as swap. Unmount it first.");
        }
        const auto system_disks = root_disks();
        if (std::find(system_disks.begin(), system_disks.end(), fs::path(*part).filename().string()) != system_disks.end()) {
            return fail(*part + " holds the running system (/). Refusing to format it.");
        }
    }
    if (!query({"findmnt", "-n", kTarget}).empty()) {
        return fail(std::string(kTarget) + " is already a mount point; unmount it first.");
    }

    // Sizes: the boot partition holds GRUB (and is the EFI system partition), the root holds the whole host system.
    const auto bytes_of = [](const std::string& dev) {
        return std::strtoull(trim(query({"lsblk", "-bndo", "SIZE", dev})).c_str(), nullptr, 10);
    };
    if (!dry_run()) {
        if (bytes_of(o.boot_part) < 64ULL * 1024 * 1024) return fail(o.boot_part + " is smaller than 64 MiB: too small for the boot partition.");
        if (bytes_of(o.root_part) < 4ULL * 1024 * 1024 * 1024) return fail(o.root_part + " is smaller than 4 GiB: too small for the system.");
        if (o.keep_boot && query({"blkid", "-s", "TYPE", "-o", "value", o.boot_part}) != "vfat") {
            return fail("--keep-boot needs " + o.boot_part + " to be FAT32 already (it is not): drop --keep-boot to format it.");
        }
    }

    step("Target partitions");
    RunOptions listing;
    listing.mutating = false;
    for (const std::string* part : {&o.boot_part, &o.root_part}) {
        const std::string parent = lsblk_field(*part, "PKNAME");
        if (!parent.empty()) run({"lsblk", "-o", "NAME,SIZE,FSTYPE,LABEL,MOUNTPOINTS", "/dev/" + parent}, listing);
    }
    warn(o.root_part + " will be formatted as " + o.fs + " (the system); ALL DATA on it is destroyed.");
    warn(o.boot_part + (o.keep_boot ? " is kept as it is (FAT32) and used for the boot files."
                                    : " will be formatted FAT32 (the boot / EFI partition); ALL DATA on it is destroyed."));
    info("Every other partition on the disk, and the systems on them, is left untouched.");
    if (!o.assume_yes && ask("Type the ROOT partition path again to confirm (" + o.root_part + ")") != o.root_part) {
        return fail("Cancelled. Nothing was changed.");
    }

    step("Formatting " + o.boot_part + " and " + o.root_part);
    layout.efi_part = o.boot_part;
    layout.root_part = o.root_part;

    // UEFI firmware boots from an EFI System Partition: mark the boot partition as one (a type flag in the
    // partition table entry only; the other partitions are not touched).
    const std::string parent = lsblk_field(o.boot_part, "PKNAME");
    std::string number;
    if (!dry_run()) number = trim(read_text("/sys/class/block/" + fs::path(o.boot_part).filename().string() + "/partition"));
    if (!parent.empty() && !number.empty()) {
        if (!run({"parted", "-s", "/dev/" + parent, "set", number, "esp", "on"}).ok()) {
            warn("Could not mark " + o.boot_part + " as an EFI System Partition; some firmware will not boot from it.");
        }
        run({"partprobe", "/dev/" + parent});
        run({"udevadm", "settle"});
    }
    return format_and_mount(o, layout, !o.keep_boot);
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

}  // namespace cccu::drive
