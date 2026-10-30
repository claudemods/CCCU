#include "rootfs.hpp"

#include "distro.hpp"
#include "incus.hpp"
#include "proc.hpp"

#include <sstream>

namespace cccu::rootfs {
namespace fs = std::filesystem;

namespace {

bool present(const fs::path& p) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(p, ec));
}

bool has_unit(const fs::path& root, const std::string& unit) {
    for (const char* dir : {"usr/lib/systemd/system", "etc/systemd/system", "lib/systemd/system"}) {
        if (present(root / dir / unit)) return true;
    }
    return false;
}

void force_symlink(const fs::path& target, const fs::path& link) {
    std::error_code ec;
    fs::create_directories(link.parent_path(), ec);
    fs::remove(link, ec);
    fs::create_symlink(target, link, ec);
    if (ec) warn("cannot create symlink " + link.string() + ": " + ec.message());
}

void mask(const fs::path& root, const std::string& unit) {
    if (has_unit(root, unit)) force_symlink("/dev/null", root / "etc/systemd/system" / unit);
}

void systemctl(const fs::path& root, const std::vector<std::string>& args) {
    std::vector<std::string> argv = {"systemctl", "--root=" + root.string()};
    argv.insert(argv.end(), args.begin(), args.end());
    if (!run(argv).ok()) warn("systemctl " + args.front() + " reported a problem (continuing)");
}

void log_write(const Result& result) {
    if (!result) warn(result.error());
}

void identity(const fs::path& root, const Options& o) {
    log_write(write_text(root / "etc/fstab", "# Intentionally empty: the container's mounts come from Incus.\n"));
    force_symlink("../proc/self/mounts", root / "etc/mtab");
    std::error_code ec;
    fs::remove(root / "etc/crypttab", ec);

    // Empty (not missing) machine-id: systemd generates a fresh one on first boot.
    log_write(write_text(root / "etc/machine-id", ""));
    fs::remove(root / "var/lib/dbus/machine-id", ec);

    log_write(write_text(root / "etc/hostname", o.name + "\n"));

    std::string hosts;
    for (const auto& line : split_lines(read_text(root / "etc/hosts"))) {
        if (line.find("127.0.1.1") == std::string::npos) hosts += line + "\n";
    }
    if (hosts.empty()) hosts = "127.0.0.1\tlocalhost\n::1\t\tlocalhost\n";
    hosts += "127.0.1.1\t" + o.name + "\n";
    log_write(write_text(root / "etc/hosts", hosts));

    for (const char* dir : {"dev", "proc", "sys", "run", "mnt", "media"}) fs::create_directories(root / dir, ec);
    for (const char* dir : {"tmp", "var/tmp"}) {
        fs::create_directories(root / dir, ec);
        fs::permissions(root / dir, fs::perms::all | fs::perms::sticky_bit, ec);
    }
}

void pacman(const fs::path& root, const Options& o) {
    // CheckSpace asks statvfs for each mount point and trips over container filesystems.
    const fs::path conf = root / "etc/pacman.conf";
    if (present(conf)) {
        std::string out;
        for (const auto& line : split_lines(read_text(conf))) {
            out += (trim(line) == "CheckSpace" ? "#CheckSpace" : line) + "\n";
        }
        log_write(write_text(conf, out));
    }
    if (o.keep_kernel) return;
    // No kernel lives in the container; stop pacman rebuilding initramfs images on every update.
    for (const char* hook : {"60-mkinitcpio-remove.hook", "90-mkinitcpio-install.hook",
                             "60-dracut-remove.hook", "90-dracut-install.hook"}) {
        if (present(root / "usr/share/libalpm/hooks" / hook)) {
            force_symlink("/dev/null", root / "etc/pacman.d/hooks" / hook);
        }
    }
}

void networking(const fs::path& root, const std::string& match) {
    const bool network_manager =
        present(root / "etc/systemd/system/multi-user.target.wants/NetworkManager.service") ||
        present(root / "etc/systemd/system/network-online.target.wants/NetworkManager-wait-online.service");
    if (network_manager) {
        info("NetworkManager is enabled in the source system; keeping it for eth0.");
        return;
    }
    log_write(write_text(root / "etc/systemd/network/10-cccu-wired.network",
                         "[Match]\nName=" + match + "\n\n[Network]\nDHCP=yes\n\n[DHCPv4]\nUseDomains=true\n"));
    systemctl(root, {"enable", "systemd-networkd.service", "systemd-resolved.service"});
    force_symlink("../run/systemd/resolve/stub-resolv.conf", root / "etc/resolv.conf");
}

void container_units(const fs::path& root, bool native) {
    // Units that need real hardware, a kernel of their own, or conflict with the host's copy.
    // The udev units are masked because the host's /run/udev is bind-mounted in instead.
    static constexpr const char* kMasked[] = {
        "systemd-remount-fs.service", "systemd-modules-load.service", "systemd-binfmt.service",
        "systemd-firstboot.service", "systemd-journald-audit.socket", "auditd.service",
        "sys-kernel-config.mount", "sys-kernel-debug.mount", "sys-kernel-tracing.mount",
        "proc-sys-fs-binfmt_misc.automount", "lvm2-monitor.service", "mdmonitor.service",
        "smartd.service", "fstrim.timer", "grub-btrfsd.service", "nvidia-persistenced.service",
        "incus.service", "incus.socket", "incus-user.socket", "incus-startup.service", "tlp.service",
    };
    for (const char* unit : kMasked) mask(root, unit);

    if (native) {
        for (const char* unit : {"systemd-udevd.service", "systemd-udev-trigger.service",
                                 "systemd-udevd-control.socket", "systemd-udevd-kernel.socket"}) {
            mask(root, unit);
        }
    }

    std::error_code ec;
    fs::remove(root / "etc/systemd/zram-generator.conf", ec);
}

std::string session_file(const fs::path& root) {
    if (present(root / "usr/share/wayland-sessions/plasma.desktop")) return "plasma.desktop";
    if (present(root / "usr/share/xsessions/plasma.desktop")) return "plasma.desktop";
    return {};
}

// SDDM / Plasma Login Manager autologin drop-in (Plasma Login Manager is an SDDM fork and reads the
// same INI layout from its own directory).
void write_autologin(const fs::path& root, const std::string& dm, const std::string& user, const std::string& session) {
    std::ostringstream conf;
    conf << "# Generated by cccu\n[Autologin]\nUser=" << user << "\n";
    if (!session.empty()) conf << "Session=" << session << "\n";
    conf << "Relogin=false\n";
    const char* dir = (dm == "plasmalogin") ? "etc/plasmalogin.conf.d" : "etc/sddm.conf.d";
    log_write(write_text(root / dir / "10-cccu-autologin.conf", conf.str()));
}

void desktop(const fs::path& root, const Options& o) {
    std::error_code ec;
    const std::string dm = pick_display_manager(root, o.dm);

    // Whatever the source had enabled is replaced explicitly below.
    fs::remove(root / "etc/systemd/system/display-manager.service", ec);

    if (o.display == Display::Session) {
        // The host runs the display manager; this system only runs Plasma when a session starts it.
        systemctl(root, {"set-default", "multi-user.target"});
        const std::string profile_d =
            "# Generated by cccu: use the host session sockets proxied into /mnt.\n"
            "if [ -n \"$XDG_RUNTIME_DIR\" ] && [ -d \"$XDG_RUNTIME_DIR\" ]; then\n"
            "    [ -S /mnt/wayland-0 ]  && ln -sf /mnt/wayland-0  \"$XDG_RUNTIME_DIR/wayland-0\"\n"
            "    [ -S /mnt/pipewire-0 ] && ln -sf /mnt/pipewire-0 \"$XDG_RUNTIME_DIR/pipewire-0\"\n"
            "fi\n"
            "if [ -S /mnt/wayland-0 ]; then\n"
            "    export WAYLAND_DISPLAY=wayland-0 XDG_SESSION_TYPE=wayland\n"
            "fi\n"
            "[ -S /mnt/pulse-native ] && export PULSE_SERVER=unix:/mnt/pulse-native\n";
        log_write(write_text(root / "etc/profile.d/cccu-container.sh", profile_d,
                             fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                                 fs::perms::others_read | fs::perms::others_exec));
        return;
    }

    if (dm.empty()) {
        warn("No display manager (plasmalogin/sddm) found; the container will boot to a console. "
             "Install one inside it with: pacman -S sddm");
        systemctl(root, {"set-default", "multi-user.target"});
        return;
    }

    info("Display manager: " + dm);
    systemctl(root, {"set-default", "graphical.target"});
    systemctl(root, {"enable", dm + ".service"});
    if (!present(root / "etc/systemd/system/display-manager.service")) {
        force_symlink("/usr/lib/systemd/system/" + dm + ".service", root / "etc/systemd/system/display-manager.service");
    }

    // The DM must wait for logind and only start when a VT is actually passed through.
    log_write(write_text(root / "etc/systemd/system" / (dm + ".service.d") / "10-cccu.conf",
                         "[Unit]\nAfter=systemd-user-sessions.service systemd-logind.service\n"
                         "ConditionPathExists=/dev/tty0\n"));

    if (!o.user.empty()) write_autologin(root, dm, o.user, session_file(root));
}

// Host side of Session mode: a "Container Plasma" login session. It waits for the container,
// starts it, then runs a kiosk compositor (cage) whose only client is the container's Plasma,
// with the compositor's Wayland socket and the user's audio sockets proxied into the container.
void write_session(const fs::path& root, const Options& o) {
    std::string script = R"SH(#!/bin/sh
# cccu session: shows the KDE Plasma of the Incus container __CONTAINER__ in this host session.
C=__CONTAINER__
CU=__CUSER__          # the user inside the container (it can have a different name than the host account)
# `cccu switch <name>` writes the container to show; without that file the ones above are used.
if [ -r /etc/cccu/active ]; then
    . /etc/cccu/active
    C=${CCCU_CONTAINER:-$C}; CU=${CCCU_USER:-$CU}
fi
if [ "$1" != "--inner" ]; then
    n=0
    until incus info "$C" >/dev/null 2>&1; do      # first boot may still be creating it
        n=$((n + 1)); [ "$n" -gt 900 ] && exit 1; sleep 1
    done
    [ "$(incus list "^$C\$" -c s --format csv)" = RUNNING ] || incus start "$C" || exit 1
    exec cage -s -- "$0" --inner
fi

# ---- inside the kiosk compositor ----
HID=$(id -u); HGID=$(id -g)
CUID=$(incus exec "$C" -- id -u "$CU") || exit 1
CGID=$(incus exec "$C" -- id -g "$CU")
proxy() {   # name host-socket container-socket
    incus config device remove "$C" "$1" >/dev/null 2>&1
    incus config device add "$C" "$1" proxy bind=container connect="unix:$2" listen="unix:$3" \
        mode=0700 security.uid="$HID" security.gid="$HGID" uid="$CUID" gid="$CGID"
}
proxy waylandSocket "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" /mnt/wayland-0
[ -S "$XDG_RUNTIME_DIR/pipewire-0" ] && proxy pipewireSocket "$XDG_RUNTIME_DIR/pipewire-0" /mnt/pipewire-0
[ -S "$XDG_RUNTIME_DIR/pulse/native" ] && proxy pulseSocket "$XDG_RUNTIME_DIR/pulse/native" /mnt/pulse-native
exec incus exec "$C" -- su -l "$CU" -s /bin/sh -c 'R=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}; ln -sf /mnt/wayland-0 "$R/wayland-0"; [ -S /mnt/pipewire-0 ] && ln -sf /mnt/pipewire-0 "$R/pipewire-0"; export XDG_RUNTIME_DIR="$R" WAYLAND_DISPLAY=wayland-0 XDG_SESSION_TYPE=wayland; [ -S /mnt/pulse-native ] && export PULSE_SERVER=unix:/mnt/pulse-native; exec dbus-run-session startplasma-wayland'
)SH";
    for (std::size_t pos; (pos = script.find("__CONTAINER__")) != std::string::npos;) script.replace(pos, 13, o.name);
    const std::string container_user = o.container_user.empty() ? o.user : o.container_user;
    for (std::size_t pos; (pos = script.find("__CUSER__")) != std::string::npos;) script.replace(pos, 9, container_user);

    log_write(write_text(root / "usr/local/bin/cccu-session", script,
                         fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                             fs::perms::others_read | fs::perms::others_exec));
    log_write(write_text(root / "usr/share/wayland-sessions/cccu.desktop",
                         "[Desktop Entry]\nName=Container Plasma\nComment=KDE Plasma from the active Incus container "
                         "(cccu switch <name> changes it)\nExec=/usr/local/bin/cccu-session\nType=Application\n"));
}

}  // namespace

Result refresh_session(const Options& options) {
    if (dry_run()) return {};
    write_session("/", options);
    return {};
}

std::string pick_display_manager(const fs::path& root, DisplayManager wanted) {
    switch (wanted) {
        case DisplayManager::None: return {};
        case DisplayManager::Sddm: return has_unit(root, "sddm.service") ? "sddm" : "";
        case DisplayManager::PlasmaLogin: return has_unit(root, "plasmalogin.service") ? "plasmalogin" : "";
        case DisplayManager::Auto: break;
    }
    // Prefer whatever the source system had enabled, then the newer Plasma Login Manager.
    std::error_code ec;
    const fs::path current = fs::read_symlink(root / "etc/systemd/system/display-manager.service", ec);
    if (!ec) {
        const std::string target = current.string();
        if (target.find("plasmalogin") != std::string::npos && has_unit(root, "plasmalogin.service")) return "plasmalogin";
        if (target.find("sddm") != std::string::npos && has_unit(root, "sddm.service")) return "sddm";
    }
    if (has_unit(root, "plasmalogin.service")) return "plasmalogin";
    if (has_unit(root, "sddm.service")) return "sddm";
    return {};
}

Result adapt(const fs::path& root, const Options& options) {
    if (dry_run()) {
        info("(dry run) would adapt " + root.string() + " for container use");
        return {};
    }
    if (!present(root / "usr") || !present(root / "etc")) {
        return fail(root.string() + " does not look like a Linux root (missing /usr or /etc)");
    }
    if (!present(root / "usr/lib/systemd/systemd") && !present(root / "lib/systemd/systemd")) {
        return fail("systemd not found in " + root.string() + "; this installer targets systemd-based Arch");
    }

    const distro::Info image = distro::detect(root);
    if (!image.reason.empty() && image.family == distro::Family::Unsupported) {
        return fail("Cannot make a container from " + image.name + ": " + image.reason + ".");
    }
    info("Container system:     " + image.name + " - " + distro::family_name(image.family));
    step("Adapting the copied system for a container");
    identity(root, options);
    pacman(root, options);
    container_units(root, options.display == Display::Native);
    networking(root, "eth0");
    desktop(root, options);
    ok("Root filesystem adapted.");
    return {};
}

Result adapt_host(const fs::path& root, const Options& o, bool running) {
    if (dry_run()) {
        info("(dry run) would configure " + root.string() + " as the host");
        return {};
    }
    step("Configuring the host system");
    std::error_code ec;
    // A running system keeps its own hostname, machine-id and network setup.
    if (!running) {
        log_write(write_text(root / "etc/hostname", o.hostname + "\n"));
        log_write(write_text(root / "etc/hosts", "127.0.0.1\tlocalhost\n::1\t\tlocalhost\n127.0.1.1\t" + o.hostname + "\n"));
        log_write(write_text(root / "etc/machine-id", ""));
    }
    fs::remove(root / "etc/systemd/system/display-manager.service", ec);

    if (!running) networking(root, "en* eth*");

    if (o.display == Display::Session) {
        // The host runs SDDM; its "Container Plasma" session shows the container's desktop.
        const std::string dm = pick_display_manager(root, o.dm);
        if (dm.empty()) {
            warn("No display manager (sddm/plasmalogin) on the host; install sddm there to get the login screen.");
            systemctl(root, {"set-default", "multi-user.target"});
        } else {
            info("Host display manager: " + dm);
            systemctl(root, {"set-default", "graphical.target"});
            systemctl(root, {"enable", dm + ".service"});
            if (!present(root / "etc/systemd/system/display-manager.service")) {
                force_symlink("/usr/lib/systemd/system/" + dm + ".service",
                              root / "etc/systemd/system/display-manager.service");
            }
            // Show the login screen only once the first-boot service has created the container;
            // until then the console shows its progress.
            log_write(write_text(root / "etc/systemd/system" / (dm + ".service.d") / "10-cccu.conf",
                                 "[Unit]\nAfter=cccu-firstboot.service\n"));
            write_session(root, o);
            // Wayland only: the greeter runs inside cage instead of on an X server.
            const char* conf_dir = (dm == "plasmalogin") ? "etc/plasmalogin.conf.d" : "etc/sddm.conf.d";
            log_write(write_text(root / conf_dir / "10-cccu-wayland.conf",
                                 "[General]\nDisplayServer=wayland\n\n[Wayland]\nCompositorCommand=cage -s --\n"));
            if (!o.user.empty()) write_autologin(root, dm, o.user, "cccu.desktop");   // the only way to log in
        }
        systemctl(root, {"enable", "getty@tty2.service"});
    } else {
        // Text-only host; the container's own display manager takes VT1 (the first-boot service
        // masks getty@tty1 once the container runs).
        systemctl(root, {"set-default", "multi-user.target"});
        systemctl(root, {"enable", "getty@tty1.service", "getty@tty2.service"});
        log_write(write_text(root / "etc/systemd/logind.conf.d/10-cccu.conf", "[Login]\nNAutoVTs=0\n"));
    }

    systemctl(root, {"enable", "incus.socket", "incus-startup.service"});
    incus::ensure_subid(root / "etc/subuid");
    incus::ensure_subid(root / "etc/subgid");
    ok("Host configured.");
    return {};
}

}  // namespace cccu::rootfs
