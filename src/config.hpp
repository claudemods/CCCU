#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cccu {

enum class DisplayManager { Auto, Sddm, PlasmaLogin, None };

// How the container's desktop reaches the screen.
//   Session - the HOST runs SDDM; the login screen offers "Container Plasma", a session that shows
//             the container's Plasma nested in a kiosk compositor (default, most robust).
//   Native  - the container runs its own display manager on a real VT (host stays on text consoles).
enum class Display { Session, Native };

// Direct = rsync straight into the instance's rootfs (dir/btrfs pools).
// Import = stage, pack as an Incus image, `incus init` from it (any storage driver).
enum class Method { Auto, Direct, Import };

struct Options {
    std::string name;
    std::string image;                     // squashfs/erofs/tar of the cloned Arch desktop system
    bool clone_system = false;             // install mode: clone the running system as the tty host
    std::string host_image;              // install mode: image of the small tty host system
    bool clone_host = false;               // alternatively clone the machine we run on
    std::string export_path;               // --export: write the host as a tar image and stop
    // Where the tty host system comes from (install mode). At most one of: clone_system, host_image,
    // host_iso, host_bootstrap; none = the root image of the live ISO that was booted.
    std::string host_iso;                  // an ISO file: its root file system is extracted
    std::string host_bootstrap;            // a built-in base package list: "arch", "debian", "ubuntu", "fedora"
    std::string host_packages_file;        // optional: extra packages (one per line) added to that list
    DisplayManager dm = DisplayManager::Auto;
    Display display = Display::Session;
    Method method = Method::Auto;

    std::string drive;                     // --drive: wipe this disk and keep the container on it
    std::string boot_part;                 // --boot-part: install to existing partitions instead of a whole drive: boot (FAT32 / EFI)
    std::string root_part;                 // --root-part: ... and the system partition
    bool keep_boot = false;                // --keep-boot: do not format the boot partition (it is FAT32 already)
    std::string fs = "ext4";            // filesystem for --drive: btrfs or ext4
    std::string pool;                      // empty = detect
    std::string incus_dir = "/var/lib/incus";
    std::string staging = "/var/tmp";

    std::string user;                      // account on the tty HOST (required): created if missing, gets Incus access, logged in automatically
    std::string user_password;             // from CCCU_USER_PASSWORD (set by the GUI); empty = ask on the terminal
    std::string root_password;             // from CCCU_ROOT_PASSWORD
    std::string container_user;          // the user INSIDE the cloned system whose desktop runs (default: same as `user`)
    std::string hostname = "cccu";  // host name of the new tty system
    std::string container_size;            // root disk of the container, e.g. "40GiB" (asked, no default)
    std::string compression;               // btrfs compression for the container storage: "", "zstd:22", "zlib:9", "lzo"...
    bool immutable = false;                // container system mounted read-only (state in separate volumes)
    std::vector<std::string> extra_excludes;
    bool keep_kernel = false;              // keep /boot and mkinitcpio/dracut pacman hooks
    bool keep_caches = false;              // keep pacman package cache and ~/.cache
    bool start = true;
    bool assume_yes = false;

    // ISO creation (cccu iso): a bootable live ISO of the container's system, built inside the container.
    std::string iso_out;                   // host folder that receives the ISO (required)
    std::string iso_name;                  // empty = automatic name
    std::string iso_tag;                   // Arch volume label, empty = 2026
    int iso_level = 3;                     // squashfs zstd level 1-22
    std::string iso_format = "squashfs";   // squashfs | erofs
    bool iso_ram = true;                   // adds a "copy to RAM" boot entry
    bool iso_clean_cache = true;           // package caches stay out of the image
    bool iso_installer = true;             // Calamares on the ISO, to install it to a disk
    bool iso_btrfs = false;                // that installer defaults to btrfs zstd:22
    bool on_console = false;               // run on VT1 like the first-boot service shows its progress
    bool on_reboot = false;                // set it up now, build on the next boot (first-boot style, on VT1)
    std::string iso_file;                  // iso-usb: the ISO to write
    std::string target;                    // duplicate: name of the new container
    bool switch_now = false;               // duplicate: make the copy the active container
    bool keep_old = false;                 // switch: leave the previous container running
};

std::optional<DisplayManager> parse_dm(std::string_view text);
std::optional<Method> parse_method(std::string_view text);
std::optional<Display> parse_display(std::string_view text);
std::string_view to_string(Display display);

}  // namespace cccu
