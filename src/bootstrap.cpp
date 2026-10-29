#include "bootstrap.hpp"

#include "proc.hpp"
#include "sources.hpp"

#include <algorithm>
#include <fstream>

namespace cccu::bootstrap {
namespace fs = std::filesystem;

namespace {

constexpr const char* kIsoMount = "/run/cccu-iso";
constexpr const char* kImageMount = "/run/cccu-iso-root";

bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string join(const std::vector<std::string>& items, const char* separator) {
    std::string out;
    for (const auto& item : items) out += (out.empty() ? "" : separator) + item;
    return out;
}

// Makes sure a tool exists in the live system, installing it with whatever package manager the live system has.
Result ensure_tool(const std::string& tool, const std::string& arch_pkg, const std::string& debian_pkg, const std::string& fedora_pkg) {
    if (have_command(tool) || dry_run()) return {};
    info("Installing " + tool + " in the live system");
    std::vector<std::string> argv;
    if (have_command("pacman")) argv = {"pacman", "-S", "--needed", "--noconfirm", arch_pkg};
    else if (have_command("apt-get")) argv = {"apt-get", "install", "-y", debian_pkg};
    else if (have_command("dnf")) argv = {"dnf", "install", "-y", fedora_pkg};
    else return fail(tool + " is missing and the live system has no known package manager to install it.");
    if (!run(argv).ok() && argv[0] == "pacman") {
        argv[1] = "-Sy";   // the sync databases may be missing on a live ISO
        run(argv);
    } else if (argv[0] == "apt-get") {
        run({"apt-get", "update"});
        run(argv);
    }
    if (!have_command(tool)) return fail(tool + " could not be installed in the live system.");
    return {};
}

// A keyring file that debootstrap uses to verify the downloaded packages. Never skipped.
Result ensure_keyring(const std::string& file, const std::string& arch_pkg, const std::string& debian_pkg) {
    std::error_code ec;
    if (fs::exists(file, ec) || dry_run()) return {};
    if (have_command("pacman")) run({"pacman", "-S", "--needed", "--noconfirm", arch_pkg});
    else if (have_command("apt-get")) run({"apt-get", "install", "-y", debian_pkg});
    if (!fs::exists(file, ec)) {
        return fail("The package signing keys " + file + " are missing in the live system, and the downloaded packages must be verified. "
                    "Install '" + (have_command("pacman") ? arch_pkg : debian_pkg) + "' (on Arch it is in the AUR) and try again.");
    }
    return {};
}

std::string os_release_value(const std::string& key) {
    for (const auto& line : split_lines(read_text("/etc/os-release"))) {
        if (line.rfind(key + "=", 0) != 0) continue;
        std::string value = line.substr(key.size() + 1);
        value.erase(std::remove(value.begin(), value.end(), '"'), value.end());
        return value;
    }
    return {};
}

std::vector<std::string> extra_packages(const std::string& file) {
    std::vector<std::string> packages;
    for (const auto& raw : split_lines(read_text(file))) {
        const std::string line = trim(raw);
        if (!line.empty() && line[0] != '#') packages.push_back(line);
    }
    return packages;
}

// ---- ISO --------------------------------------------------------------------------------------

bool is_squashfs(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    char magic[4] = {};
    in.read(magic, 4);
    return in && std::string(magic, 4) == "hsqs";
}

bool is_erofs(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    unsigned char magic[4] = {};
    in.seekg(1024);
    in.read(reinterpret_cast<char*>(magic), 4);
    return in && magic[0] == 0xE2 && magic[1] == 0xE1 && magic[2] == 0xF5 && magic[3] == 0xE0;
}

// Unmounts a mount point when it goes out of scope.
struct MountGuard {
    std::string dir;
    ~MountGuard() {
        if (dry_run() || dir.empty()) return;
        RunOptions quiet;
        quiet.quiet = true;
        run({"umount", "-R", dir}, quiet);
    }
};

// The root file system file(s) of a mounted ISO, in the order they are applied.
std::vector<fs::path> find_root_images(const fs::path& mount) {
    std::error_code ec;
    for (const char* relative : {"arch/x86_64/airootfs.sfs", "arch/x86_64/airootfs.erofs",   // Arch (archiso)
                                 "casper/filesystem.squashfs", "live/filesystem.squashfs",   // Ubuntu, Mint / Debian live
                                 "LiveOS/squashfs.img", "LiveOS/rootfs.img"}) {              // Fedora and relatives
        if (fs::is_regular_file(mount / relative, ec)) return {mount / relative};
    }
    // Newer Ubuntu images are layered: minimal, then minimal.standard on top of it.
    if (fs::is_regular_file(mount / "casper/minimal.squashfs", ec)) {
        std::vector<fs::path> layers = {mount / "casper/minimal.squashfs"};
        if (fs::is_regular_file(mount / "casper/minimal.standard.squashfs", ec)) layers.push_back(mount / "casper/minimal.standard.squashfs");
        return layers;
    }
    // Anything else: the largest squashfs / erofs image on the ISO.
    fs::path best;
    std::uintmax_t best_size = 0;
    for (fs::recursive_directory_iterator it(mount, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().string();
        if (!it->is_regular_file(ec) || !(ends_with(name, ".squashfs") || ends_with(name, ".sfs") || ends_with(name, ".erofs"))) continue;
        const std::uintmax_t size = it->file_size(ec);
        if (!ec && size > best_size) {
            best = it->path();
            best_size = size;
        }
    }
    if (!best.empty()) return {best};
    return {};
}

// A plain file system image (Fedora's LiveOS/rootfs.img): mount it and copy its content.
Result copy_from_image(const fs::path& image, const fs::path& root) {
    if (auto r = ensure_tool("rsync", "rsync", "rsync", "rsync"); !r) return r;
    std::error_code ec;
    fs::create_directories(kImageMount, ec);
    info("Mounting the root file system image " + image.string());
    if (!run({"mount", "-o", "loop,ro", image.string(), kImageMount}).ok()) return fail("could not mount " + image.string());
    MountGuard guard{kImageMount};
    if (!run({"rsync", "-aHAXS", "--info=progress2,stats2", std::string(kImageMount) + "/", root.string() + "/"}).ok()) {
        return fail("could not copy the root file system out of " + image.string());
    }
    return {};
}

Result extract_layer(const fs::path& image, const fs::path& root) {
    std::error_code ec;
    if (!is_squashfs(image) && !is_erofs(image)) return copy_from_image(image, root);   // a mountable file system image
    if (auto r = is_erofs(image) ? ensure_tool("fsck.erofs", "erofs-utils", "erofs-utils", "erofs-utils")
                                 : ensure_tool("unsquashfs", "squashfs-tools", "squashfs-tools", "squashfs-tools");
        !r) {
        return r;
    }
    if (auto r = sources::extract_image(image.string(), root); !r) return r;

    // Fedora style: the squashfs only holds LiveOS/rootfs.img, which is the real system.
    const fs::path nested = root / "LiveOS/rootfs.img";
    if (fs::is_regular_file(nested, ec)) {
        if (auto r = copy_from_image(nested, root); !r) return r;
        fs::remove_all(root / "LiveOS", ec);
    }
    return {};
}

}  // namespace

// ---- presets ----------------------------------------------------------------------------------

const std::vector<Preset>& presets() {
    static const std::vector<Preset> list = {
        {"arch", "Arch Linux  (pacstrap)", "pacstrap",
         {"base", "linux", "linux-firmware", "networkmanager", "sudo", "nano"}},
        {"debian", "Debian stable  (debootstrap)", "debootstrap",
         {"linux-image-amd64", "systemd-sysv", "dbus", "udev", "locales", "network-manager", "sudo", "nano", "ca-certificates",
          "initramfs-tools", "kmod", "iproute2"}},
        {"ubuntu", "Ubuntu 24.04 LTS  (debootstrap)", "debootstrap",
         {"linux-image-generic", "systemd-sysv", "dbus", "udev", "locales", "network-manager", "sudo", "nano", "ca-certificates",
          "initramfs-tools", "kmod", "iproute2"}},
        {"fedora", "Fedora, same release as the live system  (dnf)", "dnf",
         {"@core", "kernel", "dracut-config-generic", "NetworkManager", "sudo", "nano", "glibc-langpack-en"}},
    };
    return list;
}

const Preset* find(std::string_view id) {
    for (const auto& preset : presets()) {
        if (preset.id == id) return &preset;
    }
    return nullptr;
}

Result from_list(const Options& o, const fs::path& root) {
    const Preset* preset = find(o.host_bootstrap);
    if (!preset) return fail("Unknown base package list '" + o.host_bootstrap + "' (arch, debian, ubuntu, fedora).");

    std::vector<std::string> packages = preset->packages;
    if (!o.host_packages_file.empty()) {
        const auto extra = extra_packages(o.host_packages_file);
        info("Extra packages from " + o.host_packages_file + ": " + std::to_string(extra.size()));
        packages.insert(packages.end(), extra.begin(), extra.end());
    }
    info("Base package list: " + preset->title);
    info("Packages:          " + join(packages, " "));

    if (preset->id == "arch") {
        if (auto r = ensure_tool("pacstrap", "arch-install-scripts", "arch-install-scripts", "arch-install-scripts"); !r) return r;
        std::vector<std::string> argv = {"pacstrap", "-K", root.string()};
        argv.insert(argv.end(), packages.begin(), packages.end());
        if (!run(argv).ok()) return fail("pacstrap failed: the base system could not be installed (network and mirrors?).");
        return {};
    }

    if (preset->id == "debian" || preset->id == "ubuntu") {
        const bool debian = preset->id == "debian";
        if (auto r = ensure_tool("debootstrap", "debootstrap", "debootstrap", "debootstrap"); !r) return r;
        const std::string keyring = debian ? "/usr/share/keyrings/debian-archive-keyring.gpg" : "/usr/share/keyrings/ubuntu-archive-keyring.gpg";
        if (auto r = debian ? ensure_keyring(keyring, "debian-archive-keyring", "debian-archive-keyring")
                            : ensure_keyring(keyring, "ubuntu-keyring", "ubuntu-keyring");
            !r) {
            return r;
        }
        std::vector<std::string> argv = {"debootstrap", "--arch=amd64", "--include=" + join(packages, ","),
                                         debian ? "--components=main,contrib,non-free-firmware" : "--components=main,universe",
                                         "--keyring=" + keyring, debian ? "stable" : "noble", root.string(),
                                         debian ? "http://deb.debian.org/debian" : "http://archive.ubuntu.com/ubuntu"};
        if (!run(argv).ok()) return fail("debootstrap failed: the base system could not be installed (network?).");
        return {};
    }

    // Fedora: dnf installs into another root, using the live system's own release and repositories.
    if (os_release_value("ID") != "fedora" || !have_command("dnf")) {
        return fail("The Fedora base list is installed with dnf and needs a Fedora live system (this one is not Fedora). "
                    "Use an ISO or an image of Fedora instead, or another base list.");
    }
    std::vector<std::string> argv = {"dnf", "-y", "--installroot=" + root.string(), "--releasever=" + os_release_value("VERSION_ID"),
                                     "--setopt=install_weak_deps=False", "install"};
    argv.insert(argv.end(), packages.begin(), packages.end());
    if (!run(argv).ok()) return fail("dnf failed: the base system could not be installed (network?).");
    return {};
}

Result from_iso(const std::string& iso, const fs::path& root) {
    std::error_code ec;
    if (!fs::is_regular_file(iso, ec)) return fail("ISO not found: " + iso);
    info("ISO: " + iso + "  (" + human_bytes(fs::file_size(iso, ec)) + ")");
    if (dry_run()) {
        info("(dry run) would mount the ISO, find its root file system and extract it into " + root.string());
        return {};
    }

    fs::create_directories(kIsoMount, ec);
    if (!run({"mount", "-o", "loop,ro", iso, kIsoMount}).ok()) return fail("could not mount the ISO (is it a valid ISO, and is the loop module available?)");
    MountGuard guard{kIsoMount};

    const std::vector<fs::path> layers = find_root_images(kIsoMount);
    if (layers.empty()) {
        return fail("No root file system (squashfs / erofs / rootfs.img) found on this ISO. It may be an installer ISO that "
                    "downloads its system (netinstall): use a base package list instead.");
    }
    for (const auto& layer : layers) {
        info("Root file system: " + fs::relative(layer, kIsoMount, ec).string() + "  (" + human_bytes(fs::file_size(layer, ec)) + ")");
        if (auto r = extract_layer(layer, root); !r) return r;
    }
    return {};
}

}  // namespace cccu::bootstrap
