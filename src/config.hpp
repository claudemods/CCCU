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
    DisplayManager dm = DisplayManager::Auto;
    Display display = Display::Session;
    Method method = Method::Auto;

    std::string drive;                     // --drive: wipe this disk and keep the container on it
    std::string fs = "ext4";            // filesystem for --drive: btrfs or ext4
    std::string pool;                      // empty = detect
    std::string incus_dir = "/var/lib/incus";
    std::string staging = "/var/tmp";

    std::string user;                      // account on the tty HOST (required): created if missing, gets Incus access, logged in automatically
    std::string container_user;            // the user INSIDE the cloned system whose desktop runs (default: same as `user`)
    std::string hostname = "cccu";  // host name of the new tty system
    std::string container_size;            // root disk of the container, e.g. "40GiB" (asked, no default)
    std::string compression;               // btrfs compression for the container storage: "", "zstd:22", "zlib:9", "lzo"...
    bool immutable = false;                // container system mounted read-only (state in separate volumes)
    std::vector<std::string> extra_excludes;
    bool keep_kernel = false;              // keep /boot and mkinitcpio/dracut pacman hooks
    bool keep_caches = false;              // keep pacman package cache and ~/.cache
    bool start = true;
    bool assume_yes = false;
};

std::optional<DisplayManager> parse_dm(std::string_view text);
std::optional<Method> parse_method(std::string_view text);
std::optional<Display> parse_display(std::string_view text);
std::string_view to_string(Display display);

}  // namespace cccu
