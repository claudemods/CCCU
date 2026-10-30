#include "distro.hpp"

#include <algorithm>
#include <map>

namespace cccu::distro {
namespace fs = std::filesystem;

namespace {

// KEY=value / KEY="value" lines of an os-release file.
std::map<std::string, std::string> parse_os_release(const std::string& text) {
    std::map<std::string, std::string> values;
    for (const auto& line : split_lines(text)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == '#') continue;
        std::string value = trim(line.substr(eq + 1));
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'')) value = value.substr(1, value.size() - 2);
        values[trim(line.substr(0, eq))] = value;
    }
    return values;
}

bool has_word(const std::string& words, const std::string& word) {
    std::string padded = " " + words + " ";
    return padded.find(" " + word + " ") != std::string::npos;
}

bool uses_systemd(const fs::path& root) {
    std::error_code ec;
    for (const char* init : {"sbin/init", "usr/sbin/init"}) {
        const fs::path link = fs::read_symlink(root / init, ec);
        if (!ec && link.string().find("systemd") != std::string::npos) return true;
        ec.clear();
    }
    return fs::exists(root / "usr/lib/systemd/systemd", ec) && !fs::exists(root / "etc/init.d/rc", ec);
}

// "a b" -> "'a' 'b'" for a shell loop.
std::string words(const std::vector<std::string>& list) {
    std::string out;
    for (const auto& item : list) out += (out.empty() ? "" : " ") + item;
    return out;
}

}  // namespace

const char* family_name(Family family) {
    switch (family) {
        case Family::Arch: return "Arch family (pacman)";
        case Family::Debian: return "Debian family (apt)";
        case Family::Fedora: return "Fedora family (dnf)";
        case Family::Suse: return "openSUSE (zypper)";
        case Family::Unsupported: break;
    }
    return "unsupported";
}

Info detect(const fs::path& root) {
    std::string text = read_text(root / "etc/os-release");
    if (text.empty()) text = read_text(root / "usr/lib/os-release");
    return detect_text(text, uses_systemd(root));
}

Info detect_text(const std::string& text, bool systemd) {
    const auto os = parse_os_release(text);
    Info info;
    info.id = os.count("ID") ? os.at("ID") : "";
    info.name = os.count("PRETTY_NAME") ? os.at("PRETTY_NAME") : (info.id.empty() ? "unknown system" : info.id);
    const std::string like = os.count("ID_LIKE") ? os.at("ID_LIKE") : "";
    const auto is = [&](const char* id) { return info.id == id || has_word(like, id); };

    // Systems that cannot be driven by this installer, and why.
    if (info.id == "nixos") { info.reason = "NixOS is configured declaratively (configuration.nix), not by installing packages into a tree"; return info; }
    if (info.id == "gentoo") { info.reason = "Gentoo uses OpenRC and portage (compiled packages); no systemd by default"; return info; }
    if (info.id == "slackware") { info.reason = "Slackware has no systemd and no dependency-resolving package manager"; return info; }
    if (info.id == "void") { info.reason = "Void Linux uses runit instead of systemd"; return info; }
    if (info.id == "devuan") { info.reason = "Devuan removes systemd (sysvinit/OpenRC)"; return info; }
    if (info.id == "kde-linux" || info.id == "kdelinux") { info.reason = "KDE Linux is an immutable image: no package manager inside"; return info; }
    if (!systemd) {
        info.reason = info.name + " does not boot with systemd (Artix and MX Linux are often set up with OpenRC/sysvinit); the installer needs systemd";
        return info;
    }

    if (is("arch") || is("cachyos") || is("endeavouros") || is("garuda") || is("manjaro") || is("arcolinux")) info.family = Family::Arch;
    else if (is("debian") || is("ubuntu") || is("linuxmint") || is("pop") || is("neon") || is("mx")) info.family = Family::Debian;
    else if (is("fedora") || is("rhel") || is("centos") || is("rocky") || is("almalinux") || is("nobara")) info.family = Family::Fedora;
    else if (is("opensuse") || is("suse") || is("sles")) info.family = Family::Suse;
    else info.reason = info.name + " is not one of the known systemd-based families (Arch, Debian, Fedora, openSUSE)";
    return info;
}

std::vector<std::string> supported_list() {
    return {
        "Arch family (pacman):  Arch, CachyOS, EndeavourOS, Garuda, ArcoLinux, Manjaro",
        "Debian family (apt):   Debian, Ubuntu, Kubuntu, KDE neon, Linux Mint, Pop!_OS, MX Linux (when it boots with systemd)",
        "Fedora family (dnf):   Fedora, CentOS, AlmaLinux, Rocky Linux, RHEL, Nobara (Incus comes from Fedora/EPEL/COPR)",
        "openSUSE (zypper):     openSUSE Leap / Tumbleweed",
        "Not supported as a base: NixOS, Gentoo, Slackware, Void, Devuan, KDE Linux, Artix (no systemd or no package tree)",
    };
}

std::vector<std::string> map_packages(Family family, const std::vector<std::string>& generic) {
    // generic name -> package name in that family ("" = not needed / not available)
    struct Row { const char* generic; const char* arch; const char* debian; const char* fedora; const char* suse; };
    static const Row table[] = {
        {"incus", "incus", "incus", "incus", "incus"},
        {"grub", "grub", "grub-efi-amd64", "grub2-efi-x64", "grub2-x86_64-efi"},
        {"efibootmgr", "efibootmgr", "efibootmgr", "efibootmgr", "efibootmgr"},
        {"tar", "tar", "tar", "tar", "tar"},
        {"btrfs-progs", "btrfs-progs", "btrfs-progs", "btrfs-progs", "btrfs-progs"},
        {"squashfs-tools", "squashfs-tools", "squashfs-tools", "squashfs-tools", "squashfs"},
        {"erofs-utils", "erofs-utils", "erofs-utils", "erofs-utils", "erofs-utils"},
        {"zstd", "zstd", "zstd", "zstd", "zstd"},
        {"xz", "xz", "xz-utils", "xz", "xz"},
        {"sddm", "sddm", "sddm", "sddm", "sddm"},
        {"qt6-wayland", "qt6-wayland", "qt6-wayland", "qt6-qtwayland", "qt6-wayland"},
        {"cage", "cage", "cage", "cage", "cage"},
        {"mesa", "mesa", "libgl1-mesa-dri", "mesa-dri-drivers", "Mesa-dri"},
        {"pipewire", "pipewire", "pipewire", "pipewire", "pipewire"},
        {"pipewire-pulse", "pipewire-pulse", "pipewire-pulse", "pipewire-pulseaudio", "pipewire-pulseaudio"},
        {"wireplumber", "wireplumber", "wireplumber", "wireplumber", "wireplumber"},
    };
    std::vector<std::string> out;
    for (const auto& name : generic) {
        const Row* hit = nullptr;
        for (const auto& row : table) {
            if (name == row.generic) hit = &row;
        }
        if (!hit) {
            out.push_back(name);   // not in the table: same name everywhere (parted, rsync, ...)
            continue;
        }
        const char* mapped = family == Family::Debian ? hit->debian : family == Family::Fedora ? hit->fedora
                           : family == Family::Suse ? hit->suse : hit->arch;
        if (mapped && *mapped && std::find(out.begin(), out.end(), mapped) == out.end()) out.push_back(mapped);
    }
    return out;
}

std::string install_missing_script(Family family, const std::vector<std::string>& packages) {
    const std::string list = words(packages);
    std::string script = "set -e\nmissing=\"\"\nfor p in " + list + "; do\n    ";
    switch (family) {
        case Family::Debian: script += "dpkg-query -W -f='${Status}' \"$p\" 2>/dev/null | grep -q 'install ok installed'"; break;
        case Family::Fedora:
        case Family::Suse: script += "rpm -q \"$p\" >/dev/null 2>&1"; break;
        default: script += "pacman -Q \"$p\" >/dev/null 2>&1"; break;
    }
    script += " || missing=\"$missing $p\"\ndone\n";
    script += "if [ -z \"$missing\" ]; then echo \"Everything the host needs is already installed.\"; exit 0; fi\n"
              "echo \"Installing in the new system:$missing\"\n";
    switch (family) {
        case Family::Debian:   // a system from an ISO has empty package lists: refresh them first
            script += "DEBIAN_FRONTEND=noninteractive apt-get update || true\nDEBIAN_FRONTEND=noninteractive apt-get install -y $missing\n";
            break;
        case Family::Fedora: script += "dnf install -y $missing\n"; break;
        case Family::Suse: script += "zypper --non-interactive install $missing\n"; break;
        default:
            script += "[ -s /etc/pacman.d/gnupg/trustdb.gpg ] || { pacman-key --init; pacman-key --populate; }\n"
                      "pacman -S --noconfirm --needed $missing || pacman -Sy --noconfirm --needed $missing\n";   // an ISO has no sync databases
            break;
    }
    return script;
}

std::string initramfs_and_boot_script(Family family) {
    const std::string efi = "--target=x86_64-efi --efi-directory=/boot/efi --bootloader-id=GRUB --recheck";
    switch (family) {
        case Family::Debian:
            return "set -e\nupdate-initramfs -u -k all\ngrub-install " + efi + "\nupdate-grub\n";
        case Family::Fedora:
            return "set -e\ndracut --regenerate-all --force\ngrub2-install " + efi + "\ngrub2-mkconfig -o /boot/grub2/grub.cfg\n";
        case Family::Suse:
            return "set -e\ndracut --regenerate-all --force\ngrub2-install " + efi + "\ngrub2-mkconfig -o /boot/grub2/grub.cfg\n";
        default:
            return "set -e\nmkinitcpio -P\ngrub-install " + efi + "\ngrub-mkconfig -o /boot/grub/grub.cfg\n";
    }
}

std::string live_cleanup_script(Family family) {
    switch (family) {
        case Family::Debian:   // Debian live (live-boot), Ubuntu / Mint (casper), and their installers
            return "for p in casper live-boot live-boot-initramfs-tools live-config live-config-systemd live-tools ubiquity ubiquity-casper "
                   "ubiquity-frontend-gtk ubiquity-frontend-kde ubiquity-slideshow-ubuntu calamares calamares-settings-debian "
                   "calamares-settings-ubuntu; do\n"
                   "    dpkg -s \"$p\" >/dev/null 2>&1 && DEBIAN_FRONTEND=noninteractive apt-get purge -y \"$p\" || true\n"
                   "done\n";
        case Family::Fedora:
        case Family::Suse:
            return "for p in livesys-scripts anaconda-live dracut-live; do\n"
                   "    rpm -q \"$p\" >/dev/null 2>&1 && " + std::string(family == Family::Fedora ? "dnf remove -y" : "zypper --non-interactive remove") +
                   " \"$p\" || true\n"
                   "done\n";
        default:   // archiso: its mkinitcpio hook and preset make the initramfs look for the ISO
            return "rm -f /etc/mkinitcpio.conf.d/archiso.conf\n"
                   "for p in mkinitcpio-archiso archiso; do\n"
                   "    pacman -Q \"$p\" >/dev/null 2>&1 && pacman -Rdd --noconfirm \"$p\" || true\n"
                   "done\n"
                   "if [ -e /boot/vmlinuz-linux ] || pacman -Q linux >/dev/null 2>&1; then\n"
                   "cat > /etc/mkinitcpio.d/linux.preset <<'EOF'\n"
                   "ALL_config=\"/etc/mkinitcpio.conf\"\n"
                   "ALL_kver=\"/boot/vmlinuz-linux\"\n"
                   "PRESETS=('default' 'fallback')\n"
                   "default_image=\"/boot/initramfs-linux.img\"\n"
                   "fallback_image=\"/boot/initramfs-linux-fallback.img\"\n"
                   "fallback_options=\"-S autodetect\"\n"
                   "EOF\n"
                   "fi\n";
    }
}

std::vector<std::string> kernel_packages(Family family, const std::string& id) {
    switch (family) {
        case Family::Debian: {
            const bool ubuntu_like = id == "ubuntu" || id == "linuxmint" || id == "pop" || id == "neon";
            return {ubuntu_like ? "linux-image-generic" : "linux-image-amd64"};
        }
        case Family::Fedora: return {"kernel"};
        case Family::Suse: return {"kernel-default"};
        default: return {"linux", "linux-firmware"};
    }
}

}  // namespace cccu::distro
