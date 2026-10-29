#include "installer.hpp"

#include "distro.hpp"
#include "drive.hpp"
#include "incus.hpp"
#include "proc.hpp"
#include "profiles.hpp"
#include "rootfs.hpp"
#include "sources.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace cccu {
namespace fs = std::filesystem;

namespace {

constexpr const char* kBootstrapImage = "cccu-empty";

// Removes a staging directory when it goes out of scope, success or not.
struct ScopedDir {
    fs::path path;
    explicit ScopedDir(fs::path p) : path(std::move(p)) {}
    ~ScopedDir() {
        if (path.empty() || dry_run()) return;
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    ScopedDir(const ScopedDir&) = delete;
    ScopedDir& operator=(const ScopedDir&) = delete;
};

// ---- host preparation -------------------------------------------------------------------

std::string join_words(const std::vector<std::string>& words) {
    std::string out;
    for (const auto& w : words) out += (out.empty() ? "" : " ") + w;
    return out;
}

// Installs whatever of `tools` (plus incus and tar when asked) is missing, using the host's
// package manager. The new machine does not have to be Arch.
Result install_missing_tools(std::vector<std::string> tools, bool with_incus) {
    tools.push_back("tar");
    if (with_incus) tools.push_back("incus");

    std::vector<std::string> packages;
    for (const auto& tool : tools) {
        if (have_command(tool)) continue;
        std::string package = tool;
        if (tool == "unsquashfs") package = "squashfs-tools";
        else if (tool == "mkfs.btrfs") package = "btrfs-progs";
        else if (tool == "mkfs.ext4") package = "e2fsprogs";
        else if (tool == "fsck.erofs") package = "erofs-utils";
        if (std::find(packages.begin(), packages.end(), package) == packages.end()) packages.push_back(package);
    }
    if (packages.empty()) return {};

    std::vector<std::string> argv;
    if (have_command("pacman")) argv = {"pacman", "-S", "--needed", "--noconfirm"};
    else if (have_command("apt-get")) argv = {"apt-get", "install", "-y"};
    else if (have_command("dnf")) argv = {"dnf", "install", "-y"};
    else if (have_command("zypper")) argv = {"zypper", "-n", "install"};
    else return fail("No supported package manager found; install these yourself: " + join_words(packages));

    step("Installing missing packages: " + join_words(packages));
    argv.insert(argv.end(), packages.begin(), packages.end());
    if (!run(argv).ok()) return fail("The package manager could not install: " + join_words(packages));
    return {};
}

Result prepare_host(Options& o) {
    if (!have_command("systemctl")) return fail("systemd is required on the host.");

    if (auto r = install_missing_tools(sources::required_tools(o), true); !r) return r;

    const bool ids_changed = incus::ensure_subid("/etc/subuid") | incus::ensure_subid("/etc/subgid");

    step("Starting Incus");
    run({"systemctl", "enable", "--now", "incus.socket", "incus.service"});
    if (ids_changed) run({"systemctl", "restart", "incus.service"});

    if (dry_run()) {
        if (o.pool.empty()) o.pool = "default";
        return {};
    }
    if (!incus::daemon_ready()) return fail("Incus is installed but not answering (check: systemctl status incus).");

    auto pools = incus::storage_pools();
    if (pools.empty()) {
        step("First-time Incus setup (storage pool + network bridge)");
        if (!run({"incus", "admin", "init", "--minimal"}).ok()) return fail("incus admin init --minimal failed");
        pools = incus::storage_pools();
        if (pools.empty()) return fail("Incus has no storage pool after setup.");
    }

    // Let the user who ran sudo manage Incus without root afterwards.
    if (const char* sudo_user = std::getenv("SUDO_USER"); sudo_user && *sudo_user && std::string(sudo_user) != "root") {
        run({"usermod", "-aG", "incus-admin", sudo_user});
    }

    // The size that was asked: the container gets its own btrfs storage of exactly that size (a loop-backed
    // pool, so the size is enforced), mounted with the chosen compression.
    if (!o.container_size.empty() && o.pool.empty()) {
        const std::string pool = "cccu";
        if (std::find(pools.begin(), pools.end(), pool) == pools.end()) {
            step("Creating the container storage: btrfs, " + o.container_size +
                 (o.compression.empty() ? std::string() : ", compression " + o.compression));
            std::string mount = "user_subvol_rm_allowed";
            mount += drive::btrfs_compress_options(o.compression);
            if (!run({"incus", "storage", "create", pool, "btrfs", "size=" + o.container_size, "btrfs.mount_options=" + mount}).ok()) {
                return fail("could not create the btrfs storage pool (is btrfs-progs installed?)");
            }
            pools = incus::storage_pools();
        }
        o.pool = pool;
    }

    if (o.pool.empty()) {
        o.pool = std::find(pools.begin(), pools.end(), "default") != pools.end() ? "default" : pools.front();
    } else if (std::find(pools.begin(), pools.end(), o.pool) == pools.end()) {
        std::string list;
        for (const auto& p : pools) list += (list.empty() ? "" : ", ") + p;
        return fail("Storage pool '" + o.pool + "' not found. Available: " + list);
    }
    info("Using storage pool '" + o.pool + "' (" + incus::storage_driver(o.pool) + ")");
    return {};
}

// ---- container creation -----------------------------------------------------------------

Result create_direct(const Options& o) {
    ScopedDir work(fs::path(o.staging) / "cccu-bootstrap");
    if (auto r = incus::ensure_empty_image(kBootstrapImage, work.path); !r) return r;
    if (auto r = incus::init(o.name, kBootstrapImage, {profiles::name(o.display)}, o.pool); !r) return r;

    const fs::path root = incus::rootfs_path(o);
    std::error_code ec;
    if (!dry_run() && !fs::is_directory(root, ec)) {
        static_cast<void>(incus::remove(o.name));  // best-effort cleanup, the real error is returned
        return fail(root.string() + " not found; this pool is not directly accessible. Re-run with --method import.");
    }
    step("Installing the system into the container");
    if (auto r = sources::populate(o, root); !r) return r;
    return rootfs::adapt(root, o);
}

Result create_import(const Options& o) {
    ScopedDir stage(fs::path(o.staging) / ("cccu-" + o.name + "-stage"));
    const fs::path root = stage.path / "rootfs";
    if (!dry_run()) {
        std::error_code ec;
        fs::remove_all(stage.path, ec);
        fs::create_directories(root, ec);
        if (ec) return fail("cannot create staging directory " + root.string());
    }

    step("Unpacking the system into a staging directory");
    if (auto r = sources::populate(o, root); !r) return r;
    if (auto r = rootfs::adapt(root, o); !r) return r;

    step("Packing and importing as an Incus image");
    const fs::path tarball = stage.path / "image.tar";
    if (!dry_run()) {
        if (auto r = write_text(stage.path / "metadata.yaml", incus::image_metadata("cccu clone " + o.name)); !r) return r;
    }
    if (!run({"tar", "-C", stage.path.string(), "--numeric-owner", "--xattrs", "--xattrs-include=*", "--acls",
              "-cf", tarball.string(), "metadata.yaml", "rootfs"})
             .ok()) {
        return fail("could not pack the staged system");
    }
    const std::string alias = "cccu-" + o.name;
    if (auto r = incus::import_image(tarball, alias); !r) return r;
    auto created = incus::init(o.name, alias, {profiles::name(o.display)}, o.pool);
    static_cast<void>(incus::delete_image(alias));  // the image is only a transport
    return created;
}

// Immutable container: the system is mounted read-only; /etc, /var, /home, /root, /opt, /srv and /usr/local live
// on their own volumes (copied from the system first) and stay writable. Run before the container starts.
Result make_immutable(const Options& o) {
    step("Making the container immutable");
    const fs::path root = incus::rootfs_path(o);
    std::error_code ec;
    if (!dry_run() && !fs::is_directory(root, ec)) return fail("cannot find the container rootfs " + root.string());
    for (const char* dir : {"etc", "var", "home", "root", "opt", "srv", "usr/local"}) {
        if (!dry_run() && !fs::is_directory(root / dir, ec)) continue;
        std::string volume = o.name + "-" + dir;
        std::replace(volume.begin(), volume.end(), '/', '-');
        if (!run({"incus", "storage", "volume", "create", o.pool, volume}).ok()) return fail("could not create the volume " + volume);
        const fs::path volume_path = fs::path(o.incus_dir) / "storage-pools" / o.pool / "custom" / ("default_" + volume);
        if (!dry_run() && !fs::is_directory(volume_path, ec)) return fail("volume path not found: " + volume_path.string());
        if (auto r = copy_tree_progress((root / dir).string(), volume_path.string(), std::string("/") + dir + " into its volume"); !r) {
            return r;
        }
        if (!run({"incus", "config", "device", "add", o.name, "state-" + volume, "disk", "pool=" + o.pool,
                  "source=" + volume, std::string("path=/") + dir})
                 .ok()) {
            return fail("could not attach the volume " + volume);
        }
    }
    run({"incus", "config", "set", o.name, "raw.lxc", "lxc.mount.entry = tmpfs tmp tmpfs defaults,mode=1777 0 0"});
    if (!run({"incus", "config", "device", "set", o.name, "root", "readonly=true"}).ok()) {
        return fail("could not make the root disk read-only");
    }
    info("The container system is read-only now. To update it: incus config device set " + o.name +
         " root readonly=false, restart, update, then set it back to true.");
    return {};
}

// Dumps what is needed to see why the desktop container shows no login screen. Everything
// goes to the screen and to /var/log/cccu/container.log.
void diagnose(const std::string& name) {
    step("Diagnostics for '" + name + "'");
    const std::vector<std::vector<std::string>> checks = {
        {"incus", "list"},
        {"incus", "exec", name, "--", "sh", "-c",
         "echo \"PID1: $(cat /proc/1/comm)\"; tr '\\0' ' ' </proc/1/cmdline; echo; "
         "ls /run/systemd 2>&1 | head -5; ls -l /sbin/init /usr/bin/init 2>&1"},
        {"incus", "info", name, "--show-log"},
        {"incus", "console", name, "--show-log"},
        {"incus", "exec", name, "--", "systemctl", "list-units", "--all", "--no-pager", "--no-legend",
         "sddm*", "plasmalogin*", "display-manager*", "systemd-logind*", "dbus*"},
        {"incus", "exec", name, "--", "loginctl", "list-seats", "--no-pager"},
        {"incus", "exec", name, "--", "sh", "-c",
         "ls -l /dev/dri /dev/tty0 /dev/tty1 /dev/input /run/udev 2>&1 | head -50; echo; "
         "echo \"default target: $(systemctl get-default)\""},
        {"incus", "exec", name, "--", "journalctl", "-b", "--no-pager", "-n", "80",
         "-u", "sddm", "-u", "plasmalogin", "-u", "systemd-logind"},
    };
    for (const auto& argv : checks) run(argv);
    info("Host side: systemctl status display-manager  /  journalctl -u cccu-firstboot");
}

void print_next_steps(const Options& o, bool started) {
    info("");
    if (o.display == Display::Session) {
        info("Log in to the host's SDDM and choose the session \"Container Plasma\":");
        info("it starts '" + o.name + "' if needed and shows its KDE Plasma fullscreen.");
        info("Needs: the host user in the incus-admin group, and sddm + cage installed on the host.");
        info("Shell inside it:  incus exec " + o.name + " -- su -l <user>");
        return;
    }
    if (!started) {
        info("The container is created but not started, because your host desktop is running and");
        info("would fight it for the GPU and screen. To switch to the container's desktop:");
        info("  1. Press Ctrl+Alt+F3 and log in on a text console");
        info("  2. sudo systemctl stop display-manager     (ends the host desktop session)");
        info("  3. sudo incus start " + o.name);
        info("  4. Press Ctrl+Alt+F1 - the container's login screen appears");
    } else {
        info("Container started. This is NOT confirmed to show a login screen yet: check the diagnostics above,");
        info("and if the screen stays blank run on a text console:  incus exec " + o.name + " -- journalctl -b -u sddm -u plasmalogin");
    }
    info("Shell inside it:  sudo incus exec " + o.name + " -- su -l <user>");
    info("Inside it, pacman -Syu works like on a normal Arch install.");
}

}  // namespace

Result create_container(Options o) {
    if (!is_root()) return fail("This must run as root: sudo cccu");
    if (!valid_instance_name(o.name)) {
        return fail("Invalid container name '" + o.name + "' (letters, digits and '-', must start with a letter).");
    }

    if (o.image.empty() && !o.clone_host) {
        return fail("No system to install: give --image <file> (squashfs or tar of your cloned Arch) "
                    "or --clone-host to clone this machine.");
    }
    std::error_code ec;
    if (!o.image.empty() && !fs::is_regular_file(o.image, ec)) return fail("Image not found: " + o.image);

    if (auto r = prepare_host(o); !r) return r;

    if (incus::exists(o.name)) {
        if (!o.assume_yes && !confirm("Container '" + o.name + "' already exists. Delete and recreate it?", false)) {
            return fail("Cancelled.");
        }
        if (auto r = incus::remove(o.name); !r) return r;
    }

    step("Installing the Incus desktop profile");
    if (auto r = profiles::install(o.display); !r) return r;

    Method method = o.method;
    if (method == Method::Auto) {
        const std::string driver = incus::storage_driver(o.pool);
        method = (driver == "dir" || driver == "btrfs") ? Method::Direct : Method::Import;
        info(std::string("Copy method: ") + (method == Method::Direct ? "direct into the pool" : "image import"));
    }

    step("Creating container '" + o.name + "'");
    if (auto r = (method == Method::Direct) ? create_direct(o) : create_import(o); !r) return r;

    // The desktop container comes up on every boot of the tty host.
    if (auto r = incus::set_config(o.name, "boot.autostart", "true"); !r) warn(r.error());
    if (o.immutable) {
        if (auto r = make_immutable(o); !r) return r;
    }

    // Two systems cannot own the GPU/VT at once; never start into a running host desktop.
    // (Session mode is the opposite: the host desktop is how you reach the container.)
    const bool host_desktop = o.display == Display::Native &&
                              query({"systemctl", "is-active", "display-manager.service"}) == "active";
    const bool started = o.start && !host_desktop;
    if (started) {
        step("Starting container");
        if (auto r = incus::start(o.name); !r) return r;
        incus::wait_ready(o.name);
        diagnose(o.name);
    }
    ok("Container '" + o.name + "' is ready.");
    print_next_steps(o, started);
    return {};
}

Result setup_running_system(Options o) {
    if (!is_root()) return fail("This must run as root: sudo cccu");
    if (o.name.empty()) o.name = "desktop";
    if (!valid_instance_name(o.name)) {
        return fail("Invalid container name '" + o.name + "' (letters, digits and '-', must start with a letter).");
    }
    if (o.image.empty() && !o.clone_host) return fail("No system to install: give --image <file> or --clone-host.");

    const distro::Info host = distro::detect("/");
    if (host.family == distro::Family::Unsupported) {
        for (const auto& line : distro::supported_list()) info("  " + line);
        return fail("Cannot use " + host.name + " as the container host: " + host.reason + ".");
    }
    info("This system:          " + host.name + " - " + distro::family_name(host.family));

    step("Installing what this host needs to run the container");
    // Generic names; distro.cpp maps them to this system's package names. Installed ones are skipped.
    std::vector<std::string> generic = {"incus", "tar", "btrfs-progs", "sddm", "qt6-wayland", "cage", "mesa",
                                        "pipewire", "pipewire-pulse", "wireplumber"};
    for (const auto& tool : sources::required_tools(o)) {
        if (tool == "unsquashfs") generic.push_back("squashfs-tools");
        else if (tool == "fsck.erofs") generic.push_back("erofs-utils");
        else generic.push_back(tool);
    }
    const std::string script = distro::install_missing_script(host.family, distro::map_packages(host.family, generic));
    if (!run({"bash", "-c", script}).ok()) return fail("could not install what the host needs");

    if (!o.user.empty()) {
        const auto allowed = [](char c) { return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_' || c == '-'; };
        if (!std::all_of(o.user.begin(), o.user.end(), allowed)) {
            return fail("Invalid user name '" + o.user + "' (lowercase letters, digits, - and _ only).");
        }
        step("Desktop user '" + o.user + "'");
        const bool created = query({"id", o.user}).empty();
        // Needs the incus-admin group: the session starts and talks to the container as this user.
        if (!run({"bash", "-c",
                  "user=" + o.user + "\n"
                  "id \"$user\" >/dev/null 2>&1 || useradd -m -s /bin/bash \"$user\"\n"
                  "for g in incus-admin wheel video audio input render; do\n"
                  "    getent group $g >/dev/null && usermod -aG $g \"$user\" || true\n"
                  "done\n"})
                 .ok()) {
            return fail("could not set up the user " + o.user);
        }
        if (created) {
            info("New user: set its password.");
            if (!o.user_password.empty()) {
                if (!set_password("/", o.user, o.user_password)) return fail("could not set the password of " + o.user);
            } else {
                RunOptions tty;
                tty.interactive = true;
                run({"passwd", o.user}, tty);
            }
        }
    }

    step("Configuring this system as the container host");
    if (auto r = rootfs::adapt_host("/", o, true); !r) return r;
    return create_container(o);
}

Result export_image(const Options& o) {
    if (!is_root()) return fail("This must run as root: sudo cccu --export <file>");
    if (auto r = install_missing_tools({}, false); !r) return r;
    return sources::export_host(o, o.export_path);
}

}  // namespace cccu
