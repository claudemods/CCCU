#include "bootstrap.hpp"
#include "config.hpp"
#include "containers.hpp"
#include "hostinstall.hpp"
#include "installer.hpp"
#include "iso.hpp"
#include "menu.hpp"
#include "proc.hpp"
#include "sources.hpp"
#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace cccu;

namespace {

void usage() {
    std::cout << R"(CCCU - Claudemods Container Creator Utility
tty host + KDE desktop in an Incus system container

Typical flow
  1. On the machine to clone:   sudo cccu --export desktop.tar.zst
  2. Boot the Arch live ISO:    sudo cccu              (menu 1: Install to a drive)

Run without arguments for the menu: every setting is shown with a red NOT SET or a green SET box.

Commands
  install                   WIPE --drive, make p1 = EFI boot and p2 = system, install the tty host
                            (GRUB + Incus), and stage the desktop container for the first boot
  (default) <name>          only create the desktop container on a machine that already runs Incus
  setup                     (menu 2) set up the container on THIS running system: installs Incus, SDDM, cage
                            and the other host packages with the system's own package manager first
  --export <file>           write this machine as a tar image (.tar .tar.zst .tar.xz .tar.gz), then stop
  iso                       (menu 4) build a bootable live ISO of the container's system, INSIDE the container
                            (Arch, Debian, Fedora, openSUSE families)
  iso-usb                   write an ISO to a whole drive (--iso-file, --drive)
  containers                list the containers and which one the host session shows
  duplicate <src> <new>     copy a container (--switch makes the copy the active one)
  switch <name>             show this container in the host session (stops the previous one)
  start <name> | stop <name>

iso options
  --name <container>        the container to build from (default: desktop)
  --iso-out <folder>        host folder that receives the ISO (default: Downloads in your home folder)
  --iso-name <file>         ISO file name (default: claudebackup-<distro>-<date>.iso)
  --iso-level <1-22>        squashfs zstd level (default 3)
  --iso-format squashfs|erofs
  --iso-tag <label>         Arch volume label (default 2026)
  --no-ram                  no "copy to RAM" boot entry
  --keep-pkg-cache          leave package caches in the image
  --no-installer            do not put the Calamares installer on the ISO
  --installer-btrfs         that installer defaults to btrfs zstd:22
  --on-console              run now on VT1 like the first-boot service, with its progress display
  --on-reboot               set it up now and build on the NEXT BOOT (first-boot style, on VT1), then the
                            login screen appears; reboot yourself afterwards

install options
  --drive <dev>             drive to wipe, e.g. /dev/sda or /dev/nvme0n1
  --boot-part <dev>         ... OR install next to another system, into two EXISTING partitions: this one becomes
  --root-part <dev>         the boot (FAT32 / EFI) partition and this one the system, e.g. /dev/nvme0n1p3 and
                            /dev/nvme0n1p4. Only these two are formatted. Can run from Debian, Ubuntu ... too.
  --keep-boot               do not format the boot partition (it is FAT32 already)
  --fs ext4|btrfs           system partition filesystem (default: ext4)
  --image <file>            desktop image (squashfs/erofs/tar) made with --export
  --host-image <file>       tty host system image (default: the live ISO's own root image)
  --clone-system            instead clone the system you booted (rsync) as the tty host
  --host-iso <file>         instead use the root file system of an ISO (Arch, Debian, Ubuntu, Fedora ... live ISOs)
  --host-bootstrap <list>   instead install a fresh base system from a built-in package list:
                            arch (pacstrap), debian, ubuntu (debootstrap), fedora (dnf, needs a Fedora live system)
  --host-packages <file>    extra packages (one per line) added to that list
  --name <name>             container name (default: desktop)
  --hostname <name>         host name of the new tty system (default: cccu)
  --size <N>[GiB]           container root disk size (there is no default)
  --compress <level>        compression of the container storage: zstd:22 zstd:15 zstd:9 zstd:3 zlib:9 lzo none
  --immutable               mount the container system read-only (state lives in separate volumes)

Container options
  --image <file>            desktop image;  --clone-host  clone the running machine instead
  --display session|native|server
                            session (default): the HOST runs SDDM and offers a "Container Plasma" session
                            that shows the container's desktop (kiosk compositor cage); native: the container
                            runs its own display manager on VT1 and the host stays on text consoles;
                            server: like native, for a machine WITHOUT a usable GPU (a rented server whose only
                            screen is the BMC / iKVM chip) - Plasma Wayland draws in software, no cage
  --user <name>             the account on the tty host (required): created if missing, added to incus-admin,
                            logged in automatically
  --container-user <name>   the user inside the cloned system whose desktop runs (default: same as --user)
  --dm auto|sddm|plasmalogin|none
  --pool <name>  --method auto|direct|import  --staging <dir>
  --exclude <pattern>       extra rsync exclude (repeatable)
  --keep-kernel  --keep-caches  --no-start
  --dry-run                 print what would happen, change nothing
  -y, --yes                 do not ask for confirmation (no menu)
  -h, --help

Logs: /var/log/cccu/install.log  container.log  export.log
)";
}

int cccu_main(const std::vector<std::string>& original_args, const std::string& command_line) {
    Options o;
    bool install_mode = false;
    bool running_mode = false;
    menu::Flags flags;
    std::vector<std::string> args = original_args;
    std::string command;      // iso, iso-usb, containers, duplicate, switch, start, stop
    bool plain = false;
    for (const char* name : {"iso", "iso-usb", "containers", "duplicate", "switch", "start", "stop"}) {
        if (!args.empty() && args.front() == name) {
            command = name;
            args.erase(args.begin());
            break;
        }
    }
    if (!command.empty()) {
        // fall through to the option loop below, no install/setup word
    } else if (!args.empty() && args.front() == "install") {
        install_mode = true;
        args.erase(args.begin());
    } else if (!args.empty() && args.front() == "setup") {
        running_mode = true;
        args.erase(args.begin());
    }
    // Passwords come through the environment (set by the GUI), never through the command line.
    if (const char* p = std::getenv("CCCU_USER_PASSWORD")) o.user_password = p;
    if (const char* p = std::getenv("CCCU_ROOT_PASSWORD")) o.root_password = p;

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
        } else if (a == "--host-iso") {
            if (!value(o.host_iso)) return 2;
        } else if (a == "--host-bootstrap") {
            if (!value(o.host_bootstrap)) return 2;
            if (!bootstrap::find(o.host_bootstrap)) {
                error("Bad --host-bootstrap: " + o.host_bootstrap + " (arch, debian, ubuntu or fedora)");
                return 2;
            }
        } else if (a == "--host-packages") {
            if (!value(o.host_packages_file)) return 2;
        } else if (a == "--export") {
            if (!value(o.export_path)) return 2;
        } else if (a == "--drive") {
            if (!value(o.drive)) return 2;
        } else if (a == "--boot-part") {
            if (!value(o.boot_part)) return 2;
        } else if (a == "--root-part") {
            if (!value(o.root_part)) return 2;
        } else if (a == "--keep-boot") {
            o.keep_boot = true;
        } else if (a == "--fs") {
            if (!value(o.fs)) return 2;
            flags.fs_given = true;
        } else if (a == "--name") {
            if (!value(o.name)) return 2;
        } else if (a == "--display") {
            if (!value(v)) return 2;
            const auto parsed = parse_display(v);
            if (!parsed) {
                error("Bad --display: " + v + " (session, native or server)");
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
            o.container_size = menu::normalize_size(v);
            if (o.container_size.empty()) {
                error("Bad --size: " + v + " (a number of GiB, for example 40)");
                return 2;
            }
        } else if (a == "--compress") {
            if (!value(v)) return 2;
            o.compression = (v == "none") ? "" : v;
            flags.compress_given = true;
        } else if (a == "--immutable") {
            o.immutable = true;
        } else if (a == "--user") {
            if (!value(o.user)) return 2;
        } else if (a == "--container-user") {
            if (!value(o.container_user)) return 2;
        } else if (a == "--exclude") {
            if (!value(v)) return 2;
            o.extra_excludes.push_back(v);
        } else if (a == "--iso-out") {
            if (!value(o.iso_out)) return 2;
        } else if (a == "--iso-name") {
            if (!value(o.iso_name)) return 2;
        } else if (a == "--iso-tag") {
            if (!value(o.iso_tag)) return 2;
        } else if (a == "--iso-level") {
            if (!value(v)) return 2;
            o.iso_level = std::atoi(v.c_str());
        } else if (a == "--iso-format") {
            if (!value(o.iso_format)) return 2;
        } else if (a == "--no-ram") {
            o.iso_ram = false;
        } else if (a == "--keep-pkg-cache") {
            o.iso_clean_cache = false;
        } else if (a == "--no-installer") {
            o.iso_installer = false;
        } else if (a == "--installer-btrfs") {
            o.iso_btrfs = true;
        } else if (a == "--on-console") {
            o.on_console = true;
        } else if (a == "--on-reboot") {
            o.on_reboot = true;
        } else if (a == "--iso-file") {
            if (!value(o.iso_file)) return 2;
        } else if (a == "--switch") {
            o.switch_now = true;
        } else if (a == "--keep-old") {
            o.keep_old = true;
        } else if (a == "--plain") {
            plain = true;
        } else if (a.size() > 1 && a.front() == '-') {
            error("Unknown option: " + a);
            return 2;
        } else if (o.name.empty()) {
            o.name = a;
        } else if (command == "duplicate" && o.target.empty()) {
            o.target = a;
        } else {
            error("Unexpected argument: " + a);
            return 2;
        }
    }

    if (!is_root()) {
        error("Run with root privileges: sudo cccu");
        return 1;
    }

    // Container commands and the ISO builder: no menu. --plain output is for the GUI, so no banner either.
    if (command == "containers" && plain) {
        const auto listed = list_containers(true);
        if (!listed) {
            error(listed.error());
            return 1;
        }
        return 0;
    }
    if (command == "iso" && o.on_reboot) {
        std::vector<std::string> rest;
        for (const auto& a : original_args) {
            if (a != "--on-reboot" && a != "--on-console" && a != "--dry-run") rest.push_back(a);
        }
        if (o.iso_out.empty()) o.iso_out = default_iso_folder();
        if (std::find(rest.begin(), rest.end(), "--iso-out") == rest.end()) {
            rest.push_back("--iso-out");
            rest.push_back(o.iso_out);
        }
        set_total_steps(1);
        const auto scheduled = schedule_iso_for_boot(o, rest);
        if (!scheduled) {
            error(scheduled.error());
            return 1;
        }
        return 0;
    }
    if (command == "iso" && o.on_console && !dry_run()) {
        std::vector<std::string> rest;
        for (const auto& a : original_args) {
            if (a != "--on-console") rest.push_back(a);
        }
        const auto started = start_on_console(rest);
        if (!started) {
            error(started.error());
            return 1;
        }
        return 0;
    }

    // No arguments = the main menu; "install" = straight to its settings screen. Either way every
    // setting is shown with a red NOT SET / green SET box. --yes = unattended (the first-boot
    // service runs like this): no menu, no questions.
    const bool menu_flow = !o.assume_yes && (original_args.empty() || install_mode || running_mode);
    if (original_args.empty()) {
        const auto mode = menu::choose_mode();
        if (!mode) return 0;
        install_mode = (*mode == menu::Mode::Install);
        running_mode = (*mode == menu::Mode::Running);
        if (*mode == menu::Mode::Iso) command = "iso";
        if (!menu::settings(*mode, o, flags)) return 0;
        if (*mode == menu::Mode::Export || *mode == menu::Mode::Iso) o.assume_yes = true;   // nothing else to confirm
    } else if (menu_flow) {
        if (!menu::settings(install_mode ? menu::Mode::Install : menu::Mode::Running, o, flags)) return 0;
    }
    if (menu_flow) {
        clear_screen();
        print_banner();
    } else if (!o.assume_yes) {
        print_banner();
    }

    // Everything below is logged: the install on the live system, the container creation on the
    // host (this is what runs from the first-boot service after the reboot).
    if (!dry_run()) {
        const char* log_file = !o.export_path.empty() ? "/var/log/cccu/export.log"
                               : install_mode         ? "/var/log/cccu/install.log"
                               : command == "iso" || command == "iso-usb" ? "/var/log/cccu/iso.log"
                                                      : "/var/log/cccu/container.log";
        log_open(log_file, command_line);
        if (!log_path().empty()) info(std::string("Log file: ") + log_file);
    }

    auto finish = [](const Result& result) {
        if (result) return 0;
        error(result.error());
        return 1;
    };

    if (command == "iso") {
        if (o.iso_out.empty()) o.iso_out = default_iso_folder();
        set_total_steps(7);
        return finish(create_iso(o));
    }
    if (command == "iso-usb") {
        set_total_steps(1);
        return finish(write_iso_to_usb(o, o.drive));
    }
    if (command == "containers") return finish(list_containers(false));
    if (command == "duplicate") return finish(duplicate_container(o));
    if (command == "switch") return finish(switch_container(o));
    if (command == "start" || command == "stop") return finish(set_container_running(o.name, command == "start"));

    if (!o.export_path.empty()) {
        set_total_steps(3);
        return finish(export_image(o));
    }
    if (o.container_user.empty()) o.container_user = o.user;   // --user alone: same name in the container
    if ((install_mode || running_mode) && o.assume_yes && o.user.empty()) {
        error("Unattended setup: --user <name> is required (that user is logged in automatically).");
        return 2;
    }

    if (install_mode) {
        const bool has_target = !o.drive.empty() || (!o.boot_part.empty() && !o.root_part.empty());
        if (!has_target || o.image.empty()) {
            error("Install needs --drive <device> (wipes it) or --boot-part and --root-part (existing partitions), and --image <file> (or use the menu).");
            return 2;
        }
        if (o.container_size.empty()) {
            error("No container size given: use --size <GiB> (there is no default).");
            return 2;
        }
        set_total_steps(12);
        return finish(install_to_drive(o));
    }

    if (running_mode) {
        if (o.image.empty() && !o.clone_host) {
            error("No system to clone: give --image <file> or --clone-host.");
            return 2;
        }
        if (o.container_size.empty()) {
            error("No container size given: use --size <GiB> (there is no default).");
            return 2;
        }
        set_total_steps(10);
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
    set_total_steps(9);
    return finish(create_container(o));
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    std::string command_line = "cccu";
    for (const auto& a : args) command_line += " " + a;

    const int code = cccu_main(args, command_line);
    status_finish();
    if (!log_path().empty()) {
        if (code != 0) info("Details: " + log_path().string());
        log_close(code);
    }
    return code;
}
