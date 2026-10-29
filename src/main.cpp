#include "config.hpp"
#include "hostinstall.hpp"
#include "installer.hpp"
#include "proc.hpp"
#include "sources.hpp"
#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace claudeincus;

namespace {

void usage() {
    std::cout << R"(claudeincus-installer - tty Arch host + KDE desktop in an Incus system container

Typical flow
  1. On your Arch machine:      sudo claudeincus-installer --export desktop.tar.zst
  2. Boot the Arch live ISO:    sudo claudeincus-installer install --drive /dev/sda --image desktop.tar.zst

Commands
  install                   WIPE --drive, make p1 = EFI boot and p2 = system, install the tty host
                            (GRUB + Incus), and stage the desktop container for the first boot
  (default) <name>          only create the desktop container on a machine that already runs Incus
  (menu 2)                  set up the container on THIS running system: installs Incus, SDDM, cage and
                            the other host packages with the system's own package manager first
  --export <file>           write this machine as a tar image (.tar .tar.zst .tar.xz .tar.gz), then stop

install options
  --drive <dev>             drive to wipe, e.g. /dev/sda or /dev/nvme0n1
  --fs ext4|btrfs           system partition filesystem (default: ext4)
  --image <file>            desktop image (squashfs/erofs/tar) made with --export
  --host-image <file>       tty host system image (default: the live ISO's own root image)
  --clone-system            instead clone the system you booted (rsync) as the tty host
  --name <name>             container name (default: desktop)
  --hostname <name>         host name of the new tty system (default: claudeincus)
  --size <N>[GiB]           container root disk size (asked in the menu; there is no default)
  --compress <level>        compression of the container storage: zstd:22 zstd:15 zstd:9 zstd:3 zlib:9 lzo none
  --immutable               mount the container system read-only (state lives in separate volumes)

Container options
  --image <file>            desktop image;  --clone-host  clone the running machine instead
  --display session|native  session (default): the HOST runs SDDM and offers a "Container Plasma" session
                            that shows the container's desktop; native: the container runs its own
                            display manager on VT1 and the host stays on text consoles
  --user <name>             the account on the tty host (required): created if missing, added to incus-admin,
                            logged in automatically
  --container-user <name>   the user inside the cloned system whose desktop runs (default: same as --user)
  --dm auto|sddm|plasmalogin|none
  --pool <name>  --method auto|direct|import  --staging <dir>
  --exclude <pattern>       extra rsync exclude (repeatable)
  --keep-kernel  --keep-caches  --no-start
  --dry-run                 print what would happen, change nothing
  -y, --yes                 do not ask for confirmation
  -h, --help
)";
}

// ---- the questions of the installer ------------------------------------------------------------

// Compression of the container's btrfs storage (btrfs mount option compress=<choice>).
std::string ask_compression() {
    info("");
    info("Compression for the container's btrfs storage (saves space, costs some CPU):");
    info("  1) zstd:22   strongest");
    info("  2) zstd:15");
    info("  3) zstd:9");
    info("  4) zstd:3    fast and still good");
    info("  5) zlib:9");
    info("  6) lzo       fastest");
    info("  7) none");
    for (int attempt = 0; attempt < 5; ++attempt) {
        const std::string c = ask("Choose (1-7)", "1");
        if (c == "1") return "zstd:22";
        if (c == "2") return "zstd:15";
        if (c == "3") return "zstd:9";
        if (c == "4") return "zstd:3";
        if (c == "5") return "zlib:9";
        if (c == "6") return "lzo";
        if (c == "7") return "none";
        error("Type a number from 1 to 7.");
    }
    return "zstd:22";
}
// "40", "40G", "40GB", "40GiB" -> "40GiB"; "" when it is not a size.
std::string normalize_size(std::string text) {
    text = trim(text);
    std::size_t i = 0;
    while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.')) ++i;
    if (i == 0) return {};
    const std::string number = text.substr(0, i);
    std::string unit = trim(text.substr(i));
    for (char& c : unit) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (!unit.empty() && unit != "g" && unit != "gb" && unit != "gib") return {};
    if (std::stod(number) < 1.0) return {};
    return number + "GiB";
}

// The container size is always asked; there is no default. The hint comes from the image itself.
std::string ask_container_size(const std::string& image) {
    std::error_code ec;
    const unsigned long long packed = std::filesystem::file_size(image, ec);
    const unsigned long long unpacked = sources::estimate_unpacked_bytes(image);
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    const unsigned long long hint = static_cast<unsigned long long>(std::ceil(static_cast<double>(unpacked) * 1.5 / kGiB));

    info("");
    info("Container size = the root disk of the container: your whole desktop, its apps and updates live in it.");
    if (!ec) {
        info("Your image is " + human_bytes(packed) + " (" + sources::describe_image(image) + "); unpacked it needs roughly " +
             human_bytes(unpacked) + ".");
        info("Leave room for updates and new apps: about " + std::to_string(hint < 8 ? 8 : hint) + " GiB or more is a sensible size.");
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        const std::string size = normalize_size(ask("Container size in GiB"));
        if (size.empty()) {
            error("Type a number of GiB, for example " + std::to_string(hint < 8 ? 8 : hint) + ".");
            continue;
        }
        if (unpacked > 0 && std::stod(size) * kGiB < static_cast<double>(unpacked)) {
            warn("That is smaller than the unpacked image (" + human_bytes(unpacked) + "): the system will not fit.");
            if (!confirm("Use " + size + " anyway?", false)) continue;
        }
        return size;
    }
    return {};
}

bool valid_hostname(const std::string& name) {
    if (name.empty() || name.size() > 63 || name.front() == '-' || name.back() == '-') return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') return false;
    }
    return true;
}

// The container's own questions: name, size (no default, hinted from the image), compression, immutable.
void ask_container_choices(Options& o, bool compress_given) {
    if (o.name.empty()) o.name = ask("Container name", "desktop");
    if (o.container_size.empty()) o.container_size = ask_container_size(o.image);
    if (!compress_given) {
        o.compression = ask_compression();
        if (o.compression == "none") o.compression.clear();
    }
    if (!o.immutable) {
        info("");
        info("Immutable container: its system (/usr, /bin, /lib ...) is mounted read-only, so nothing can change it by accident.");
        info("/etc, /var, /home, /root, /opt, /srv and /usr/local stay writable (their own volumes).");
        info("To update the system later:  incus config device set " + o.name + " root readonly=false   (then restart it).");
        o.immutable = confirm("Make the container immutable?", false);
    }
}
// Two names, because they can differ: the user INSIDE the cloned system (its desktop is what runs; nothing is
// created there) and the account on the tty HOST (created if missing, logged in automatically).
void ask_desktop_user(Options& o) {
    if (o.container_user.empty() && !o.user.empty()) o.container_user = o.user;   // --user alone: same name in both

    if (o.container_user.empty()) {
        const std::vector<std::string> users = sources::image_users(o.image);   // empty image = this running system
        info("");
        info("User in the cloned system: the container is a clone, so its users already exist (nothing is created there).");
        std::string fallback;
        if (!users.empty()) {
            info("Users found in the cloned system:");
            for (std::size_t i = 0; i < users.size(); ++i) info("  " + std::to_string(i + 1) + ") " + users[i]);
            fallback = users.front();
        }
        for (int attempt = 0; attempt < 5 && o.container_user.empty(); ++attempt) {
            std::string answer = ask(users.empty() ? "Username in the cloned system" : "Username in the cloned system (number or name)", fallback);
            if (answer.empty()) {
                error("A username is required.");
                continue;
            }
            if (std::all_of(answer.begin(), answer.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
                const std::size_t n = static_cast<std::size_t>(std::stoul(answer));
                if (n >= 1 && n <= users.size()) answer = users[n - 1];
            }
            if (!users.empty() && std::find(users.begin(), users.end(), answer) == users.end()) {
                warn("'" + answer + "' is not a user of the cloned system: the container could not start the desktop as that user.");
                if (!confirm("Use it anyway?", false)) continue;
            }
            o.container_user = answer;
        }
    }
    if (o.user.empty()) {
        info("");
        info("Account on the tty host: it can have a different name. It is created if missing, gets Incus access and is");
        info("logged in automatically at boot.");
        for (int attempt = 0; attempt < 5 && o.user.empty(); ++attempt) {
            o.user = ask("Username on the tty host", o.container_user);
            if (o.user.empty()) error("A username is required: the tty host logs this account in automatically.");
        }
    }
}
}  // namespace
int main(int argc, char** argv) {
    Options o;
    bool install_mode = false;
    bool running_mode = false;
    bool fs_given = false;
    bool compress_given = false;
    std::vector<std::string> args(argv + 1, argv + argc);
    if (!args.empty() && args.front() == "install") {
        install_mode = true;
        args.erase(args.begin());
    }

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= args.size()) {
                error("Missing value for " + a);
                return false;
            }
            out = args[++i];
            return true;
        };
        std::string v;

        if (a == "-h" || a == "--help") {
            usage();
            return 0;
        } else if (a == "-y" || a == "--yes") {
            o.assume_yes = true;
        } else if (a == "--dry-run") {
            set_dry_run(true);
        } else if (a == "--keep-kernel") {
            o.keep_kernel = true;
        } else if (a == "--keep-caches") {
            o.keep_caches = true;
        } else if (a == "--no-start") {
            o.start = false;
        } else if (a == "--clone-host") {
            o.clone_host = true;
        } else if (a == "--image") {
            if (!value(o.image)) return 2;
        } else if (a == "--clone-system") {
            o.clone_system = true;
        } else if (a == "--host-image") {
            if (!value(o.host_image)) return 2;
        } else if (a == "--export") {
            if (!value(o.export_path)) return 2;
        } else if (a == "--drive") {
            if (!value(o.drive)) return 2;
        } else if (a == "--fs") {
            if (!value(o.fs)) return 2;
            fs_given = true;
        } else if (a == "--name") {
            if (!value(o.name)) return 2;
        } else if (a == "--display") {
            if (!value(v)) return 2;
            const auto parsed = parse_display(v);
            if (!parsed) {
                error("Bad --display: " + v + " (session or native)");
                return 2;
            }
            o.display = *parsed;
        } else if (a == "--dm") {
            if (!value(v)) return 2;
            const auto parsed = parse_dm(v);
            if (!parsed) {
                error("Bad --dm: " + v);
                return 2;
            }
            o.dm = *parsed;
        } else if (a == "--method") {
            if (!value(v)) return 2;
            const auto parsed = parse_method(v);
            if (!parsed) {
                error("Bad --method: " + v);
                return 2;
            }
            o.method = *parsed;
        } else if (a == "--pool") {
            if (!value(o.pool)) return 2;
        } else if (a == "--staging") {
            if (!value(o.staging)) return 2;
        } else if (a == "--hostname") {
            if (!value(o.hostname)) return 2;
        } else if (a == "--size") {
            if (!value(v)) return 2;
            o.container_size = normalize_size(v);
            if (o.container_size.empty()) {
                error("Bad --size: " + v + " (a number of GiB, for example 40)");
                return 2;
            }
        } else if (a == "--compress") {
            if (!value(v)) return 2;
            o.compression = (v == "none") ? "" : v;
            compress_given = true;
        } else if (a == "--immutable") {
            o.immutable = true;
        } else if (a == "--user") {
            if (!value(o.user)) return 2;
        } else if (a == "--container-user") {
            if (!value(o.container_user)) return 2;
        } else if (a == "--exclude") {
            if (!value(v)) return 2;
            o.extra_excludes.push_back(v);
        } else if (a.size() > 1 && a.front() == '-') {
            error("Unknown option: " + a);
            return 2;
        } else if (o.name.empty()) {
            o.name = a;
        } else {
            error("Unexpected argument: " + a);
            return 2;
        }
    }

    if (!is_root()) {
        error("Run with root privileges: sudo claudeincus-installer");
        return 1;
    }

    // The banner for the interactive installer (not for the unattended first-boot run).
    if (!o.assume_yes && (args.empty() || install_mode)) print_banner();

    // Started with no arguments: ask what to do.
    if (args.empty() && !install_mode) {
        info("\n1. Install to a drive (wipe it: EFI boot + tty host + desktop container)");
        info("2. Set up the container on this running system (installs what the host needs)");
        info("3. Export this machine as a desktop image");
        const std::string choice = ask("Choose (1-3)", "1");
        if (choice == "1") {
            install_mode = true;
        } else if (choice == "2") {
            running_mode = true;
        } else if (choice == "3") {
            o.export_path = ask("Output file", "desktop.tar.zst");
        } else {
            error("Invalid choice.");
            return 2;
        }
    }

    // Everything below is logged: the install on the live system, the container creation on the
    // host (this is what runs from the first-boot service after the reboot).
    if (!dry_run()) {
        const char* log_file = !o.export_path.empty() ? "/var/log/claudeincus/export.log"
                               : install_mode         ? "/var/log/claudeincus/install.log"
                                                      : "/var/log/claudeincus/container.log";
        log_open(log_file);
        if (!log_path().empty()) info(std::string("Log file: ") + log_file);
    }

    auto finish = [](const Result& result) {
        if (result) return 0;
        error(result.error());
        return 1;
    };

    if (!o.export_path.empty()) {
        const int code = finish(export_image(o));
        if (code == 0) ok("Image written: " + o.export_path);
        return code;
    }
    if (o.container_user.empty()) o.container_user = o.user;   // --user alone: same name in the container
    if ((install_mode || running_mode) && o.assume_yes && o.user.empty()) {
        error("Unattended setup: --user <name> is required (that user is logged in automatically).");
        return 2;
    }

    if (install_mode) {
        if (o.drive.empty()) {
            RunOptions listing;
            listing.mutating = false;
            run({"lsblk", "-d", "-o", "NAME,SIZE,MODEL,TRAN"}, listing);
            o.drive = ask("Drive to WIPE and install onto (e.g. /dev/sda)");
        }
        if (o.image.empty()) o.image = ask("Path to the desktop image made with --export");
        if (o.host_image.empty() && !o.clone_system && !o.assume_yes) {
            const std::string source = ask("tty host system: 'running' = clone the system you booted, or a path to an image "
                                           "(empty = the live ISO's image, or the running system if there is none)");
            if (source == "running") o.clone_system = true;
            else o.host_image = source;
        }
        if (!o.assume_yes) {
            // Like a normal installer: who and what is this machine, then how big the container is.
            for (int attempt = 0; attempt < 5; ++attempt) {
                o.hostname = ask("Hostname for the new system", o.hostname);
                if (valid_hostname(o.hostname)) break;
                error("A hostname has letters, digits and - only.");
            }
            ask_desktop_user(o);
            if (!fs_given) o.fs = ask("System filesystem (ext4 or btrfs)", o.fs);
            ask_container_choices(o, compress_given);
        } else if (o.container_size.empty()) {
            error("Unattended install: the container size must be given with --size (there is no default).");
            return 2;
        }
        if (o.container_size.empty()) {
            error("No container size given.");
            return 2;
        }
        return finish(install_to_drive(o));
    }

    if (running_mode) {
        if (o.image.empty() && !o.clone_host) {
            o.image = ask("Path to the system image (squashfs/tar made with --export), or 'host' to clone THIS running system");
            if (o.image == "host") {
                o.image.clear();
                o.clone_host = true;
            }
        }
        if (!o.assume_yes) {
            ask_desktop_user(o);
            // The size hint comes from the image; when cloning this system there is no file to look at.
            if (o.clone_host && o.container_size.empty()) {
                info("");
                info("Container size = the root disk of the container. Cloning this system copies what is installed now,");
                info("so leave room for it plus updates and new apps (see: df -h /).");
                for (int attempt = 0; attempt < 5 && o.container_size.empty(); ++attempt) {
                    o.container_size = normalize_size(ask("Container size in GiB"));
                    if (o.container_size.empty()) error("Type a number of GiB, for example 40.");
                }
            }
            ask_container_choices(o, compress_given);
        } else if (o.container_size.empty()) {
            error("Unattended setup: the container size must be given with --size (there is no default).");
            return 2;
        }
        if (o.container_size.empty()) {
            error("No container size given.");
            return 2;
        }
        return finish(setup_running_system(o));
    }

    if (o.name.empty()) o.name = ask("Container name", "desktop");
    if (o.image.empty() && !o.clone_host) {
        o.image = ask("Path to your cloned Arch image (squashfs or tar), or 'host' to clone this machine");
        if (o.image == "host") {
            o.image.clear();
            o.clone_host = true;
        }
    }
    return finish(create_container(o));
}
