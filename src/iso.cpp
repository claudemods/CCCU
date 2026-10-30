#include "iso.hpp"

#include "distro.hpp"
#include "incus.hpp"
#include "iso_embed.hpp"
#include "proc.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace cccu {
namespace fs = std::filesystem;

namespace {

constexpr const char* kOutDevice = "cccu-iso-out";        // Incus disk device: the host folder inside the container
constexpr const char* kOutMount = "/mnt/cccu-iso-out";    // ... and where it appears (/mnt is left out of the ISO)
constexpr const char* kWorkDir = "/var/tmp/cccu-iso-work"; // clone mount points and the ISO tree; removed afterwards
constexpr const char* kEnginePath = "/usr/local/bin/cccu-iso-engine";   // the ISO engine, pushed in on every run
constexpr const char* kProvisionDir = "/opt/cccu-iso";     // provisioning script + installer files, removed after use
constexpr const char* kStateDir = "/var/lib/cccu-iso";     // marker: tools for this setup are installed

bool safe_word(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-';
    });
}

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool write_blob(const fs::path& path, const iso_embed::Blob& blob) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(blob.begin, static_cast<std::streamsize>(blob.size()));
    return static_cast<bool>(out);
}

// A shell script run inside the container.
RunResult in_container(const std::string& name, const std::string& script, const RunOptions& options = {},
                       const std::vector<std::string>& env = {}) {
    std::vector<std::string> argv = {"incus", "exec", name};
    for (const auto& e : env) {
        argv.push_back("--env");
        argv.push_back(e);
    }
    argv.insert(argv.end(), {"--", "sh", "-c", script});
    return run(argv, options);
}

RunOptions quiet_query() {
    RunOptions options;
    options.mutating = false;
    options.capture = true;
    options.quiet = true;
    return options;
}

// Puts things back whatever happens: the output folder device and the work folder in the container.
struct Cleanup {
    std::string name;
    bool device = false;
    bool work = false;
    ~Cleanup() {
        if (dry_run()) return;
        RunOptions quiet;
        quiet.quiet = true;
        if (device) run({"incus", "config", "device", "remove", name, kOutDevice}, quiet);
        if (work) in_container(name, std::string("umount -R ") + kWorkDir + " 2>/dev/null; rm -rf " + kWorkDir, quiet);
    }
};

std::string family_key(distro::Family family) {
    switch (family) {
        case distro::Family::Arch: return "arch";
        case distro::Family::Debian: return "debian";
        case distro::Family::Fedora: return "fedora";
        case distro::Family::Suse: return "opensuse";
        case distro::Family::Unsupported: break;
    }
    return {};
}

// The kernel the ISO boots: the container has no kernel of its own running, so a container shares the host's.
// The ISO needs the one INSTALLED in the container: the host's release when the container has it, else the newest.
constexpr const char* kFindKernel = R"SH(
u=$(uname -r)
if [ -e "/usr/lib/modules/$u/modules.dep" ]; then echo "$u"; exit 0; fi
best=""
for d in /usr/lib/modules/*/; do
    [ -e "${d}modules.dep" ] || continue
    v=${d%/}; v=${v##*/}
    best=$(printf '%s\n%s\n' "$best" "$v" | sed '/^$/d' | sort -V | tail -n 1)
done
[ -n "$best" ] && echo "$best"
)SH";

std::string find_kernel(const std::string& name) {
    return trim(in_container(name, kFindKernel, quiet_query()).out);
}

bool kernel_name_ok(const std::string& kv) {
    return !kv.empty() && std::all_of(kv.begin(), kv.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '+' || c == '-';
    });
}

}  // namespace

Result create_iso(Options o) {
    if (!is_root()) return fail("This must run as root: sudo cccu iso");
    if (o.name.empty()) o.name = "desktop";
    if (!valid_instance_name(o.name)) return fail("Invalid container name '" + o.name + "'.");
    if (o.iso_out.empty()) return fail("No output folder: give --iso-out <folder> (the ISO is saved there).");
    if (o.iso_level < 1 || o.iso_level > 22) return fail("--iso-level is 1 to 22.");
    if (o.iso_format != "squashfs" && o.iso_format != "erofs") return fail("--iso-format is squashfs or erofs.");
    if (!o.iso_name.empty() && !safe_word(o.iso_name)) return fail("The ISO name may only contain letters, digits, '.', '_' and '-'.");
    if (!o.iso_tag.empty() && !safe_word(o.iso_tag)) return fail("The ISO tag may not contain spaces or quotes.");

    std::error_code ec;

    // 1 -----------------------------------------------------------------------------------------
    step("Checking the container '" + o.name + "'");
    if (!dry_run() && !incus::daemon_ready()) return fail("Incus is not answering (systemctl status incus).");
    if (!dry_run() && !incus::exists(o.name)) return fail("There is no container named '" + o.name + "'. See: cccu containers");
    const std::string state = query({"incus", "list", "^" + o.name + "$", "-c", "s", "--format", "csv"});
    if (state != "RUNNING") {
        info("The container is " + (state.empty() ? std::string("not running") : lower(state)) + "; starting it.");
        if (auto r = incus::start(o.name); !r) return r;
        incus::wait_ready(o.name);
    }

    // 2 -----------------------------------------------------------------------------------------
    step("Detecting the system inside the container");
    const std::string os_release = in_container(o.name, "cat /etc/os-release 2>/dev/null || cat /usr/lib/os-release", quiet_query()).out;
    const bool systemd = in_container(o.name, "test -d /run/systemd/system", quiet_query()).ok();
    const distro::Info system = distro::detect_text(os_release, systemd);
    if (system.family == distro::Family::Unsupported && !dry_run()) {
        return fail("Cannot build an ISO of " + system.name + ": " + system.reason + ".");
    }
    const std::string family = family_key(system.family);
    info("System:  " + system.name + " - " + distro::family_name(system.family));
    std::string iso_name = o.iso_name;
    if (iso_name.empty()) {
        char stamp[64];
        const std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%Y-%m-%d_%H%M", std::localtime(&now));
        iso_name = "claudebackup-" + (system.id.empty() ? std::string("linux") : lower(system.id)) + "-" + stamp + ".iso";
    } else if (iso_name.size() < 4 || iso_name.substr(iso_name.size() - 4) != ".iso") {
        iso_name += ".iso";
    }
    info("ISO:     " + (fs::path(o.iso_out) / iso_name).string());

    // 3 -----------------------------------------------------------------------------------------
    step("Connecting the output folder");
    if (!dry_run()) {
        fs::create_directories(o.iso_out, ec);
        if (ec) return fail("cannot create " + o.iso_out + ": " + ec.message());
    }
    Cleanup cleanup;
    cleanup.name = o.name;
    RunOptions quiet;
    quiet.quiet = true;
    run({"incus", "config", "device", "remove", o.name, kOutDevice}, quiet);   // a leftover of an aborted run
    if (!run({"incus", "config", "device", "add", o.name, kOutDevice, "disk", "source=" + fs::absolute(o.iso_out).string(),
              std::string("path=") + kOutMount})
             .ok()) {
        return fail("could not connect " + o.iso_out + " to the container");
    }
    cleanup.device = true;

    // 4 -----------------------------------------------------------------------------------------
    step("Checking the kernel inside the container");
    std::string kernel = dry_run() ? "(kernel)" : find_kernel(o.name);
    if (kernel.empty()) {
        warn("The container has no kernel (the desktop clone is made without one). Installing one - the ISO has to boot something.");
        const std::string script = distro::install_missing_script(system.family, distro::kernel_packages(system.family, system.id));
        if (!run({"incus", "exec", o.name, "--", "bash", "-c", script}).ok()) return fail("could not install a kernel in the container");
        kernel = find_kernel(o.name);
        if (kernel.empty()) return fail("A kernel package was installed but no /usr/lib/modules/<version>/modules.dep exists.");
    }
    if (!dry_run() && !kernel_name_ok(kernel)) return fail("Unexpected kernel name '" + kernel + "'.");
    info("Kernel:  " + kernel);

    // 5 -----------------------------------------------------------------------------------------
    step("Preparing the ISO tools in the container");
    // The engine is part of this program: it is written out and pushed into the container, nothing is compiled there.
    const fs::path stage = "/var/tmp/cccu-iso-push";   // not /tmp: a snap-packaged incus cannot read it
    struct StageGuard {
        fs::path path;
        ~StageGuard() {
            std::error_code ignore;
            fs::remove_all(path, ignore);
        }
    } stage_guard{stage};
    if (!dry_run()) {
        fs::remove_all(stage, ec);
        fs::create_directories(stage / "resources", ec);
        if (!write_blob(stage / "cccu-iso-engine", iso_embed::engine())) return fail("could not write the ISO engine to " + stage.string());
        in_container(o.name, "mkdir -p /usr/local/bin", quiet_query());
        if (!run({"incus", "file", "push", "--mode", "0755", (stage / "cccu-iso-engine").string(), o.name + kEnginePath}).ok()) {
            return fail("could not copy the ISO engine into the container");
        }
    }

    // The tools the engine runs (xorriso, mksquashfs, bootloaders, Calamares) come from the container's own package
    // manager, through the provisioning part of claudebackup's build.sh (--root-phase). Done once per setup.
    const std::string marker = std::string(kStateDir) + "/ready-" + CCCU_VERSION + "-" + family + "-" + (o.iso_installer ? "c" : "n") +
                               (o.iso_btrfs ? "b" : "e") + (o.iso_format == "erofs" ? "f" : "s");
    const bool ready = !dry_run() && in_container(o.name, "test -e '" + marker + "'", quiet_query()).ok();
    if (ready) {
        info("ISO tools are already installed in this container.");
    } else {
        if (!dry_run()) {
            if (!write_blob(stage / "build.sh", iso_embed::provision())) return fail("could not write the provisioning script");
            // Everything the provisioning needs travels inside cccu, so it never has to download or find a file:
            // the Calamares files, and for the non-Arch families the bootloader files (Arch has its own in the engine).
            if (o.iso_installer) {
                if (!write_blob(stage / "resources/calamares-files.zip", iso_embed::calamares()) ||
                    !write_blob(stage / "resources/claudemods.zip", iso_embed::claudemods())) {
                    return fail("could not write the installer files");
                }
            }
            if (iso_embed::bootloaders().size() > 0 && !write_blob(stage / "resources/bootloaders.tar.gz", iso_embed::bootloaders())) {
                return fail("could not write the bootloader files");
            }
            in_container(o.name, std::string("rm -rf ") + kProvisionDir + " && mkdir -p " + kProvisionDir);
            if (!run({"incus", "file", "push", "-r", (stage / "build.sh").string(), (stage / "resources").string(), o.name + kProvisionDir + "/"}).ok()) {
                return fail("could not copy the provisioning files into the container");
            }
        }
        const auto provision = [&](bool installer) {
            const std::string script =
                std::string("set -e\ncd ") + kProvisionDir + "\nsh build.sh --root-phase " + family + " " + (installer ? "1" : "0") + " " +
                (o.iso_btrfs ? "1" : "0") + " " + (o.iso_format == "erofs" ? "1" : "0") + " 0\n";
            return in_container(o.name, script, {}, {"CBL_NO_COMPILER=1"}).ok();
        };
        bool installer_done = o.iso_installer;
        if (!provision(o.iso_installer)) {
            if (!o.iso_installer) return fail("installing the ISO tools in the container failed (see above)");
            // The ISO itself does not need the installer: a distribution without a Calamares package still gets its ISO.
            warn("The Calamares installer could not be installed in this container; building the ISO without it.");
            if (!provision(false)) return fail("installing the ISO tools in the container failed (see above)");
            installer_done = false;
        }
        if (!dry_run()) {
            const std::string done = std::string(kStateDir) + "/ready-" + CCCU_VERSION + "-" + family + "-" + (installer_done ? "c" : "n") +
                                     (o.iso_btrfs ? "b" : "e") + (o.iso_format == "erofs" ? "f" : "s");
            in_container(o.name, std::string("mkdir -p ") + kStateDir + " && touch '" + done + "'; rm -rf " + kProvisionDir, quiet_query());
        }
    }

    // 6 -----------------------------------------------------------------------------------------
    step("Building the ISO (this takes a while)");
    cleanup.work = true;
    std::string build = std::string(kEnginePath) + " --yes --name '" + iso_name + "' --out " + kOutMount + " --work " + kWorkDir +
                        " --tree-dir " + kWorkDir + "/isotree --level " + std::to_string(o.iso_level) + " --format " + o.iso_format +
                        (o.iso_ram ? " --ram" : " --no-ram") + (o.iso_clean_cache ? " --clean-cache" : " --keep-cache");
    if (!o.iso_tag.empty()) build += " --tag '" + o.iso_tag + "'";
    if (!in_container(o.name, build, {}, {"CBL_KERNEL=" + kernel, "TERM=" + std::string(color_enabled() ? "xterm-256color" : "dumb")}).ok()) {
        return fail("The ISO build failed (see above; /var/log/claudebackup-linux.log inside the container has the details).");
    }

    // 7 -----------------------------------------------------------------------------------------
    step("Checking the result");
    const fs::path result = fs::path(o.iso_out) / iso_name;
    if (!dry_run()) {
        const auto size = fs::file_size(result, ec);
        if (ec || size == 0) return fail("The build reported success but " + result.string() + " does not exist.");
        run({"chown", "--reference=" + fs::absolute(o.iso_out).string(), result.string()}, quiet);
        ok("ISO saved: " + result.string() + "  (" + human_bytes(size) + ")");
    }
    info("Test it in a VM (UEFI) first. Write it to a USB stick with:  cccu iso-usb --iso-file <file> --drive /dev/sdX");
    return {};
}

Result start_on_console(const std::vector<std::string>& args) {
    if (!is_root()) return fail("This must run as root.");
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return fail("could not find this program: " + ec.message());

    std::vector<std::string> argv = {
        "systemd-run", "--unit=cccu-iso", "--description=CCCU: build a live ISO", "--no-block", "--collect",
        "-p", "StandardInput=tty", "-p", "StandardOutput=tty", "-p", "StandardError=tty",
        "-p", "TTYPath=/dev/tty1", "-p", "TTYReset=yes", "-p", "TTYVHangup=yes",
        "-p", "Conflicts=getty@tty1.service",
        "-p", "ExecStartPre=-/usr/bin/kill -s RTMIN+21 1",     // silence boot messages while the progress is drawn
        "-p", "ExecStopPost=-/usr/bin/kill -s RTMIN+20 1",
        "-p", "ExecStopPost=-/usr/bin/systemctl start getty@tty1.service",
        "-E", "TERM=linux", exe.string()};
    argv.insert(argv.end(), args.begin(), args.end());
    if (std::find(args.begin(), args.end(), "--yes") == args.end()) argv.push_back("--yes");
    if (!run(argv).ok()) return fail("could not start the console service (is cccu-iso already running? systemctl status cccu-iso)");
    RunOptions quiet;
    quiet.quiet = true;
    run({"chvt", "1"}, quiet);
    ok("The ISO build runs on the console (VT1, Ctrl+Alt+F1) like the first-boot installer.");
    info("Follow it also with:  journalctl -u cccu-iso -f");
    return {};
}

Result schedule_iso_for_boot(const Options& o, const std::vector<std::string>& args) {
    if (!is_root()) return fail("This must run as root.");
    const std::string name = o.name.empty() ? "desktop" : o.name;
    if (!valid_instance_name(name)) return fail("Invalid container name '" + name + "'.");
    if (o.iso_out.empty()) return fail("No output folder: give --iso-out <folder>.");
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return fail("could not find this program: " + ec.message());
    if (!dry_run()) {
        if (!incus::daemon_ready()) return fail("Incus is not answering (systemctl status incus).");
        if (!incus::exists(name)) return fail("There is no container named '" + name + "'. See: cccu containers");
        fs::create_directories(o.iso_out, ec);
        if (ec) return fail("cannot create " + o.iso_out + ": " + ec.message());
    }

    const auto quote = [](const std::string& s) {
        std::string out = "'";
        for (char c : s) out += (c == '\'') ? std::string("'\\''") : std::string(1, c);
        return out + "'";
    };
    // systemd expands $VAR and %specifiers in ExecStart: double them so the shell sees them untouched.
    const auto unit_escape = [](const std::string& s) {
        std::string out;
        for (char c : s) out += (c == '$') ? std::string("$$") : (c == '%') ? std::string("%%") : std::string(1, c);
        return out;
    };
    std::string command = quote(exe.string());
    for (const auto& a : args) command += " " + quote(a);
    if (std::find(args.begin(), args.end(), "--yes") == args.end()) command += " --yes";
    // A failed build must not trap the next boots: the service always removes itself, and the error stays readable.
    const std::string wrapper = command + "; rc=$?; if [ $rc -ne 0 ]; then echo; echo \"The ISO build failed (exit $rc). "
                                          "Log: /var/log/cccu/iso.log - continuing in 30 s.\"; sleep 30; fi; exit $rc";

    // The login screen waits for the build, like it waits for the first-boot service.
    std::string dm = "sddm.service";
    const fs::path link = fs::read_symlink("/etc/systemd/system/display-manager.service", ec);
    if (!ec && !link.filename().empty()) dm = link.filename().string();
    const std::string dropin_dir = "/etc/systemd/system/" + dm + ".d";
    const std::string dropin = dropin_dir + "/10-cccu-iso.conf";

    const std::string unit =
        "[Unit]\n"
        "Description=CCCU: build a live ISO (set up before the reboot)\n"
        "Wants=network-online.target incus.service\n"
        "After=network-online.target incus.service incus.socket getty@tty1.service\n"
        "Conflicts=getty@tty1.service\n"
        "ConditionPathExists=/var/lib/cccu/iso-pending\n\n"
        "[Service]\n"
        "Type=oneshot\n"
        "TimeoutStartSec=infinity\n"
        "StandardInput=tty\n"
        "StandardOutput=tty\n"
        "StandardError=tty\n"
        "TTYPath=/dev/tty1\n"
        "TTYReset=yes\n"
        "TTYVHangup=yes\n"
        "Environment=TERM=linux\n"
        "ExecStartPre=-/usr/bin/kill -s RTMIN+21 1\n"
        "ExecStart=/bin/sh -c " + quote(unit_escape(wrapper)) + "\n"
        "ExecStopPost=/usr/bin/rm -f /var/lib/cccu/iso-pending " + dropin + "\n"
        "ExecStopPost=/usr/bin/systemctl disable cccu-iso-boot.service\n"
        "ExecStopPost=-/usr/bin/kill -s RTMIN+20 1\n"
        "ExecStopPost=-/usr/bin/systemctl --no-block start getty@tty1.service\n\n"
        "[Install]\n"
        "WantedBy=multi-user.target\n";

    step("Setting up the ISO build for the next boot");
    if (dry_run()) {
        info("(dry run) would write cccu-iso-boot.service running: " + command);
        return {};
    }
    fs::create_directories("/var/lib/cccu", ec);
    fs::create_directories(dropin_dir, ec);
    if (auto r = write_text("/var/lib/cccu/iso-pending", "pending\n"); !r) return r;
    if (auto r = write_text(dropin, "[Unit]\nAfter=cccu-iso-boot.service\n"); !r) return r;
    if (auto r = write_text("/etc/systemd/system/cccu-iso-boot.service", unit); !r) return r;
    run({"systemctl", "daemon-reload"});
    if (!run({"systemctl", "enable", "cccu-iso-boot.service"}).ok()) return fail("could not enable cccu-iso-boot.service");
    ok("Ready. Reboot: the ISO is built on the console (VT1) with the progress display, then the login screen appears.");
    info("To cancel before rebooting:  systemctl disable cccu-iso-boot.service");
    return {};
}

Result write_iso_to_usb(const Options& o, const std::string& drive) {
    if (!is_root()) return fail("This must run as root.");
    std::error_code ec;
    if (o.iso_file.empty() || !fs::is_regular_file(o.iso_file, ec)) return fail("ISO file not found: " + o.iso_file);
    if (drive.rfind("/dev/", 0) != 0) return fail("A drive is a device path such as /dev/sdb.");
    if (!dry_run() && !fs::exists(drive, ec)) return fail(drive + " does not exist.");

    // Never overwrite the drive the running system is on.
    const std::string mounts = query({"lsblk", "-nro", "MOUNTPOINTS", drive});
    for (const auto& line : split_lines(mounts)) {
        for (const char* critical : {"/", "/boot", "/boot/efi", "/home", "/usr", "/var"}) {
            if (trim(line) == critical) return fail(drive + " holds a mounted system folder (" + critical + "); refusing to overwrite it.");
        }
    }
    step("Writing " + fs::path(o.iso_file).filename().string() + " to " + drive);
    RunOptions quiet;
    quiet.quiet = true;
    for (const auto& part : split_lines(query({"lsblk", "-nrpo", "NAME", drive}))) {
        if (trim(part) != drive) run({"umount", trim(part)}, quiet);
    }
    RunOptions dd;
    dd.progress_total = fs::file_size(o.iso_file, ec);
    if (!run({"dd", "if=" + o.iso_file, "of=" + drive, "bs=4M", "conv=fsync"}, dd).ok()) return fail("writing to " + drive + " failed");
    run({"sync"});
    ok("ISO written to " + drive + ".");
    return {};
}

}  // namespace cccu
