<p align="center">
<img src="https://i.postimg.cc/JhMRf2RZ/claudemods-03-17-2025.gif">

<div align="center">

  <a href="https://www.linux.org" target="_blank"><img src="https://img.shields.io/badge/OS-Linux-e06c75?style=for-the-badge&logo=linux" /></a>
  <a href="https://isocpp.org" target="_blank"><img src="https://img.shields.io/badge/LANG-C++23-00599C?style=for-the-badge&logo=cplusplus" /></a>
  <a href="https://linuxcontainers.org/incus/" target="_blank"><img src="https://img.shields.io/badge/CONTAINERS-Incus-3b82f6?style=for-the-badge&logo=linuxcontainers&logoColor=white" /></a>
  <a href="https://kde.org/plasma-desktop/" target="_blank"><img src="https://img.shields.io/badge/DESKTOP-KDE_Plasma-1d99f3?style=for-the-badge&logo=kde&logoColor=white" /></a>

  <a href="https://archlinux.org" target="_blank"><img src="https://img.shields.io/badge/DISTRO-Arch-56b6c2?style=for-the-badge&logo=arch-linux" /></a>
  <a href="https://ubuntu.com/" target="_blank"><img src="https://img.shields.io/badge/DISTRO-Ubuntu-E95420?style=for-the-badge&logo=Ubuntu" /></a>
  <a href="https://www.debian.org" target="_blank"><img src="https://img.shields.io/badge/DISTRO-Debian-CE0058?style=for-the-badge&logo=Debian" /></a>
  <a href="https://getfedora.org" target="_blank"><img src="https://img.shields.io/badge/DISTRO-Fedora-51A2DA?style=for-the-badge&logo=fedora" /></a>
  <a href="https://www.opensuse.org" target="_blank"><img src="https://img.shields.io/badge/DISTRO-openSUSE-73BA25?style=for-the-badge&logo=opensuse" /></a>
  <a href="https://linuxmint.com" target="_blank"><img src="https://img.shields.io/badge/DISTRO-Linux_Mint-87CF3E?style=for-the-badge&logo=linuxmint&logoColor=white" /></a>

  <a href="https://claude.com/claude-code" target="_blank">
    <img src="https://img.shields.io/badge/Built_Using-Claude_Code-D97757?style=for-the-badge&logo=claude&logoColor=white" alt="Built Using Claude Code">
  </a>

  ## [ Guide ](#usage)

  ## [ Support Me ](https://www.paypal.com/paypalme/claudemods?country.x=GB&locale)

</div>

# CCCU - Claudemods Container Creator Utility

> *Sailing The Seven Seas like Penguin's Eggs, Remastersys, Refracta, Systemback and father Knoppix!*

Clone the Linux system you already run into an **Incus system container** with its own sized (and optionally
compressed and immutable) storage, and boot straight into its KDE Plasma desktop on a minimal tty host.

## Contents

1. [Overview](#overview)
2. [Quick start](#quick-start)
3. [The interface](#the-interface)
4. [Build](#build)
5. [Desktop version (Qt6)](#desktop-version-qt6)
6. [Live ISOs and several containers](#live-isos-and-several-containers)
7. [Usage](#usage)
8. [The tty host system](#the-tty-host-system)
9. [Container storage](#container-storage)
10. [Supported systems](#supported-systems)
11. [Logging](#logging)
12. [Source layout](#source-layout)
13. [FAQ](#faq)
14. [Status and roadmap](#status-and-roadmap)

## Overview

CCCU takes a Linux system - a running machine, or an image made with `--export` - and turns it into an Incus
system container: a full systemd system that shares the host kernel (it is **not** a virtual machine, so GPU,
input and sound work natively). The container is hosted on a small tty system, either on a freshly partitioned
drive or on the machine you are running now. The configured user is logged in automatically into the
container's KDE Plasma session.

```
drive / running system
 +- EFI boot (FAT32, GRUB)                (drive install only)
 +- tty host --- Incus --- SDDM + cage
        +- container "desktop"            your cloned Linux: KDE Plasma and all your apps
               +- own btrfs storage       sized by you, compressed, optionally immutable
```

## Quick start

```bash
# 0. build and install, directly (dependencies are installed for you)
bash build.sh

# 1. on the machine you want to clone
sudo cccu --export desktop.tar.zst

# 2. on the machine that will host the container
#    (or boot an Arch live ISO and choose "Install to a drive" to wipe a drive)
sudo cccu              # terminal version
                       # or click "CCCU" in the application menu: the Qt6 desktop version (see Build)
```

## The interface

This is the terminal version; the [desktop version](#desktop-version-qt6) offers the same modes and status boxes
in a window.

Running `cccu` without arguments opens the main menu. Each mode then shows a **settings screen**: every setting
is a row with a status box - **green `SET`** when it has a value, **red `NOT SET`** when a required value is
still missing. Type a row number to change it, `S` to start (only possible when nothing is red), `Q` to quit.

```
+- INSTALL TO A DRIVE ---------------------------------------------------------+
|  #  SETTING                VALUE                             STATUS          |
+------------------------------------------------------------------------------+
|  1  Target drive (WIPED)   /dev/sda                           [   SET   ]    |
|  2  Desktop image          -                                  [ NOT SET ]    |
|  3  tty host system        live ISO image (default)           [   SET   ]    |
|  ...                                                                         |
+------------------------------------------------------------------------------+
|  [ MISSING ]  1 required setting not set yet                                 |
+------------------------------------------------------------------------------+
```

Every copy shows what it does. A file copy (the desktop image, the installer, `resolv.conf`) prints the source,
the destination and the size, then a live line with percentage, bytes copied, speed and ETA, and ends with a
summary (size, time, average speed, how much stayed sparse). A folder copy prints the number of files and
folders and the total size first, and the same summary at the end. Cloning with rsync shows its overall progress
and the file/size statistics; extracting an image shows the percentage read. Other long silent commands (such as
writing an export image) report a real percentage, measured from the bytes the command has read.

While an installation runs, every step prints a header with a counter (`[3/12]`), an overall progress bar and
the elapsed time. A live status line (spinner, step time, total time) stays at the bottom and keeps moving even
while a long command is silent. Messages carry level tags - `[ INFO ]`, `[  OK  ]`, `[ WARN ]`, `[ FAIL ]` -
and what the commands print is shown in dark green. Colour and the live line switch off automatically when the
output is not a terminal or `NO_COLOR` is set.

**First boot after an installation.** The `cccu-firstboot` service runs on a real terminal (VT1), so the same
progress display appears live while the container is created; when it finishes, the login screen (SDDM, or the
container's own display manager in native mode) takes over the screen. The
output is also available with `journalctl -u cccu-firstboot -f`.

## Build

Requires GCC 14+ (or Clang 17+) with C++23 and CMake 3.25+. The terminal tool needs no other libraries; the Qt6
desktop version additionally needs Qt 6 Widgets.

### Build the GUI (Qt6 desktop version)

```bash
bash build.sh
```

That is the whole process - it installs directly. It installs the dependencies (including Qt 6), builds **`cccu-gui`**
together with the terminal tool `cccu` and the ISO engine, installs them system-wide and adds **CCCU** to your
application menu. Then:

* click **CCCU** in the application menu, **or**
* double-click `build/cccu-gui` in the file manager (if you used `--no-install`).

It asks for your password in the normal desktop dialog - no terminal or `sudo` needed to start it.
With `--no-install` the build is the same, just not installed: `bash build.sh --no-install`.

### Build the terminal tool only

```bash
bash build.sh --no-gui        # no Qt needed
sudo ./build/cccu
```

### What build.sh does

`build.sh` detects your system (Arch, Debian/Ubuntu, Fedora or openSUSE family), installs the build and runtime
dependencies with its package manager, and builds into `build/`.

| Option | |
|---|---|
| `--no-install` | build only; the default is to install system-wide (CCCU in the application menu) |
| `--no-gui` | terminal tool only (no Qt6 needed) |
| `--deps-only` | only install the dependencies |
| `--no-deps` | build without touching the package manager |
| `--clean` | remove `build/` first |
| `-j <N>` | parallel jobs (default: all cores) |

It checks for a C++23 compiler (GCC 14+ or Clang 17+) and stops with a clear message if the system's is too old.
The dependencies are the compiler, CMake 3.25+, `rsync`, `tar`, `zstd`, `squashfs-tools`, `parted`, `dosfstools`,
`e2fsprogs`, `btrfs-progs`, `curl` (the ISO boot files are fetched once and carried inside `cccu`), a static libc for
the ISO engine (`glibc-static` on Fedora, `glibc-devel-static` on openSUSE), and for the GUI Qt 6 Widgets and polkit. Incus itself is installed later by CCCU
when it sets up a host.

**By hand - terminal version** (`cccu`):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo ./build/cccu
```

**By hand - Qt6 desktop version** (`cccu-gui`, additionally needs Qt 6 Widgets, e.g. `qt6-base` on Arch,
`qt6-base-dev` on Debian/Ubuntu, `qt6-qtbase-devel` on Fedora). One command builds both programs:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCCCU_BUILD_GUI=ON
cmake --build build -j
sudo cmake --install build        # adds "CCCU" with an icon to the application menu
```

Without installing, double-click `build/cccu-gui` in the file manager.

## Desktop version (Qt6)

`cccu-gui` offers every mode on ONE screen, no scrolling: tabs on top (*Install to a drive*, *Set up on this system*,
*Export*, *Create ISO*, *Containers*), the settings of the tab in a two-column grid, and progress, log and buttons at the
bottom. Every setting has the same status box as the terminal menu - green `SET`, red `NOT SET`, grey `OPTIONAL` -
and **Start** unlocks only when nothing required is red. A progress bar follows the steps, and the log shows the
installer output live (errors red, warnings amber).

* It is a front-end: it runs the `cccu` tool (found next to `cccu-gui`, or in `PATH`) with your settings, so both
  versions behave identically and write the same logs.
* It is started by clicking - no terminal. It needs root (it partitions drives and installs packages), so on
  start it asks for your password with the desktop's own authentication dialog (polkit / `pkexec`) and reopens
  with the rights it needs. If that is not available the window opens but Start stays disabled and says why.
* Before a drive install it asks you to type the device path to confirm the wipe.
* **Export** always asks where to save the image (a "Save as" dialog, starting in your home folder). The tool
  shows the full path, checks the free space, writes the image with a live percentage, then verifies that the
  file exists, hands it to your user, and the window reports the final path and size. A missing file is
  reported as a failure, never as success.
* The user and root passwords are passed to the tool through its environment and set with `chpasswd`; they never
  appear on a command line or in the log.
* **Dry run** shows what would happen without changing anything. **Cancel** stops the running operation.

## Live ISOs and several containers

**Create ISO** (menu 4, GUI tab *Create ISO*) turns the system inside a container into a bootable live ISO. The
claudebackup remaster engine is **part of CCCU's own code** (`src/remaster`; it is built as `cccu-iso-engine` and
carried inside the `cccu` binary, together with the Calamares files and the boot files): there are no sources to copy
and nothing is compiled in the container. `cccu iso` pushes the engine into the container and runs it **there**, so the ISO is
made with the container's own kernel, package manager and tools: Arch, Debian, Fedora and openSUSE families (the
ones CCCU can clone). CCCU installs the ISO tools (xorriso, squashfs, bootloaders, Calamares) and, when the clone
has none, a kernel inside the container first; the finished ISO is copied out to your folder. Progress of the
squashfs / erofs step is shown live like every other step (a percentage in the window, the live line on the console). The
engine is linked statically, so it starts in any container; on Fedora/openSUSE building it needs `glibc-static`
(`glibc-devel-static`).

```bash
sudo cccu iso --name desktop --iso-out ~/isos --iso-level 6 --iso-format squashfs
sudo cccu iso --name desktop --iso-out ~/isos --on-reboot      # set up now, build on the next boot (the only way the GUI builds)
sudo cccu iso --name desktop --iso-out ~/isos --on-console     # build now, progress on VT1 like the first-boot service
sudo cccu iso-usb --iso-file ~/isos/claudebackup-arch-2026-10-30_1200.iso --drive /dev/sdX
```

The service shows the animated squashfs/erofs bar, then a green "Creating the ISO" bar for the ISO stage. When the
ISO is saved, the PC reboots after a 5 second countdown (a failed build does not reboot: it shows the error, waits
30 seconds and goes on to the login screen).

The ISO is saved in `~/Downloads` unless you choose another folder: the window asks where to save it every time
(starting in Downloads), the terminal menu and `--iso-out` default to it.

Settings: ISO name and tag, image format (squashfs / erofs), zstd level, RAM boot entry, package-cache cleaning,
Calamares installer (ext4 or btrfs zstd:22). The ISO needs free space inside the container of roughly twice its size.

**Containers** (menu 5, GUI tab *Containers*): keep several containers and choose which one the host session shows.

```bash
sudo cccu containers
sudo cccu duplicate desktop desktop-test --switch   # full copy (incus copy), then make it the active one
sudo cccu switch desktop                            # "Container Plasma" now shows this one; the previous is stopped
```

The active container is recorded in `/etc/cccu/active`, which the host's `cccu-session` reads at login.
Immutable containers cannot be duplicated yet.

## Usage

| Menu entry | What it does |
|---|---|
| 1. Install to a drive | Wipes a drive: GPT, EFI + system partition (ext4 or btrfs), GRUB, Incus, and creates the container on the first boot |
| 2. Set up on this system | Installs what the host needs (Incus, `btrfs-progs`, SDDM, `cage`, GPU and audio packages) with the system's own package manager, creates the user and the container. Reboot afterwards: SDDM becomes the display manager |
| 3. Export this machine | Writes the running system as a `.tar.zst` image to clone elsewhere |
| 4. Create a live ISO | Builds a bootable live ISO of the container's system, inside the container (see above) |
| 5. Containers | Lists the containers, duplicates one, switches the one the host session shows |

Everything can also be given on the command line; `--yes` runs unattended without the menu. `cccu install ...`
is menu 1, `cccu setup ...` is menu 2.

```bash
sudo cccu install --drive /dev/sda --image desktop.tar.zst --user alice --size 60 --compress zstd:22 --yes
sudo cccu setup --clone-host --user alice --size 60 --yes
```

Unattended runs set passwords from the environment (`CCCU_USER_PASSWORD`, `CCCU_ROOT_PASSWORD`); without them
the tool asks on the terminal.

| Option | Description |
|---|---|
| `--drive <dev>` | drive to wipe (install mode) |
| `--image <file>` | system image (`.tar` `.tar.zst` `.tar.xz` `.tar.gz`, squashfs, erofs) |
| `--clone-host` | clone the running machine instead of an image |
| `--hostname <name>` | hostname of the new tty system (default `cccu`) |
| `--user <name>` | account on the tty host: created if missing, added to `incus-admin`, logged in automatically |
| `--container-user <name>` | user inside the cloned system whose desktop runs (default: same as `--user`) |
| `--size <N>[GiB]` | container disk size - there is no default |
| `--compress <level>` | `zstd:22` `zstd:15` `zstd:9` `zstd:3` `zlib:9` `lzo` `none` |
| `--immutable` | mount the container's system read-only |
| `--fs ext4\|btrfs` | host system filesystem |
| `--keep-kernel` `--keep-caches` `--no-start` `--dry-run` `--yes` | see `cccu --help` |
| `iso --iso-out <dir>` | build a live ISO (`--iso-name` `--iso-level` `--iso-format` `--iso-tag` `--no-ram` `--keep-pkg-cache` `--no-installer` `--installer-btrfs` `--on-console` `--on-reboot`) |
| `iso-usb --iso-file <f> --drive <dev>` | write an ISO to a drive |
| `containers` `duplicate <src> <new> [--switch]` `switch <name>` `start` `stop` | manage the containers |

### Install next to another system (existing partitions)

`--drive` wipes a whole disk. To keep another system on the same disk (say Debian on p1/p2), give two EXISTING
partitions instead - the window has *Install to > Two existing partitions*, the terminal menu has the same choice:

```bash
sudo cccu install --boot-part /dev/nvme0n1p3 --root-part /dev/nvme0n1p4 --image desktop.tar.zst \
     --host-iso my-arch-clone.iso --user alice --size 60 --fs btrfs --yes
```

Only those two partitions are formatted: the boot one as FAT32 (it is marked as the EFI system partition; `--keep-boot`
leaves it as it is when it is FAT32 already) and the root one as `--fs`. The partition table and everything else on the
disk stay untouched, GRUB goes to the boot partition with its own firmware entry named `CCCU` (the other system's entry
stays), and mounted partitions, the running system and partitions that are too small are refused. To get the other
system into the new GRUB menu, install `os-prober` in the new system and run `grub-mkconfig`.

CCCU can run from an installed Debian, Ubuntu, Fedora or openSUSE as well as from a live ISO: it installs the tools
it needs (`parted`, `dosfstools`, `squashfs-tools` ...) with the system's own package manager. The tty host can come
from an ISO made by claudebackup (its `LiveOS/rootfs.img` is found and unpacked like any other live ISO).

## The tty host system

A drive install needs a small system to run Incus on. "tty host system" in the settings chooses where it comes
from:

| Source | Option | What happens |
|---|---|---|
| Live ISO you booted (default) | - | the root image of the running live ISO is installed |
| Clone of the running system | `--clone-system` | the system you booted is copied with rsync |
| System image file | `--host-image <file>` | a squashfs / erofs / tar image is extracted |
| **ISO file** | `--host-iso <file>` | the ISO is mounted, its root file system found and extracted |
| **Base package list** | `--host-bootstrap <list>` | a fresh base system is downloaded and installed from a built-in list |

**ISO files.** CCCU finds the root file system by itself: Arch (`airootfs.sfs` / `.erofs`), Debian live
(`live/filesystem.squashfs`), Ubuntu and Mint (`casper/filesystem.squashfs`, also the layered `minimal` images),
Fedora and relatives (`LiveOS/squashfs.img` / `rootfs.img`), or else the largest squashfs/erofs image on the ISO.
Because a live system boots by searching for its ISO, CCCU afterwards removes the live-boot parts (archiso,
casper, live-boot, dracut-live, the installer) and installs a kernel if the ISO has none, so the system boots
from the drive. Netinstall ISOs that only download their system have no root file system: use a base list.

**Base package lists.** You choose one; nobody types a package list. The list is built in and shown in the
menu (and as a tooltip in the GUI):

| List | Installed with | Packages |
|---|---|---|
| `arch` | `pacstrap` | `base linux linux-firmware networkmanager sudo nano` |
| `debian` | `debootstrap`, Debian stable | kernel, systemd, dbus, udev, locales, network-manager, sudo, nano, initramfs-tools, ... |
| `ubuntu` | `debootstrap`, Ubuntu 24.04 LTS | the same, with the Ubuntu kernel |
| `fedora` | `dnf --installroot`, same release as the live system | `@core kernel dracut-config-generic NetworkManager sudo nano` |

The tool needed (`pacstrap`, `debootstrap`) is installed in the live system when missing, and the packages are
verified with the distribution's signing keys (they are never skipped; if the keys are missing, CCCU stops and
says which package provides them - on Arch the Debian/Ubuntu keyrings are in the AUR). The Fedora list needs a
Fedora live system. `--host-packages <file>` adds your own extra packages (one per line) to a list. After the base
system is in place, CCCU installs Incus, the bootloader and the rest of what the host needs, exactly as for any
other source.

```bash
sudo cccu install --drive /dev/sda --image desktop.tar.zst --host-iso ~/Downloads/debian-live.iso --user alice --size 60 --yes
sudo cccu install --drive /dev/sda --image desktop.tar.zst --host-bootstrap debian --user alice --size 60 --yes
```

## Container storage

The container gets its **own btrfs storage pool** (loop-backed, so the size is enforced). When you choose the
size, CCCU reads the image and suggests one:

```
Your image is 4.2 GiB (zstd-compressed tar); unpacked it needs roughly 12.6 GiB.
Leave room for updates and new apps: about 19 GiB or more is a sensible size.
```

| Compression | Notes |
|---|---|
| `zstd:22` | strongest (default in the menu) |
| `zstd:15`, `zstd:9`, `zstd:3` | strong, balanced, fast |
| `zlib:9`, `lzo` | alternatives |
| `none` | no compression |

With **immutable** enabled the container's root disk is read-only. `/etc`, `/var`, `/home`, `/root`, `/opt`,
`/srv` and `/usr/local` live on their own volumes and stay writable. To update the system:

```bash
incus config device set desktop root readonly=false   # restart, update, then set it back to true
```

## Supported systems

The installer needs systemd inside the tty host and inside the container, and detects the system from
`/etc/os-release`; an unsupported one is refused with the reason before anything is changed.

| Family | Systems | Package manager | Status |
|---|---|---|---|
| Arch | Arch, CachyOS, EndeavourOS, Garuda, ArcoLinux, Manjaro | `pacman` | primary target |
| Debian | Debian, Ubuntu, Kubuntu, KDE neon, Linux Mint, Pop!_OS, MX Linux (systemd boot only) | `apt` | implemented, needs field testing |
| Fedora | Fedora, CentOS, AlmaLinux, Rocky Linux, RHEL, Nobara | `dnf` | implemented, needs field testing |
| openSUSE | Leap, Tumbleweed | `zypper` | implemented, needs field testing |
| Not supported | NixOS, Gentoo, Slackware, Void, Devuan, KDE Linux, Artix | - | no systemd, or no installable package tree |

## Logging

Every run writes a structured log to `/var/log/cccu/`:

| File | Content |
|---|---|
| `install.log` | the drive installation |
| `container.log` | container creation (including the first-boot run) and diagnostics |
| `export.log` | exports |
| `iso.log` | ISO builds and writing an ISO to USB |

One record per line - `date time LEVEL text`:

```
============================================================================
 CCCU v1.0 30-10-2026  -  Claudemods Container Creator Utility
 started : 2026-10-29 14:03:11.482
 host    : live
 command : cccu install --drive /dev/sda --image desktop.tar.zst --yes
============================================================================
2026-10-29 14:03:12.044 STEP  [2/12] Partitioning and formatting /dev/sda
2026-10-29 14:03:12.051 CMD   #5 wipefs -a /dev/sda
2026-10-29 14:03:12.402 OUT   #5 | /dev/sda: 2 bytes were erased at offset 0x00000200
2026-10-29 14:03:12.440 DONE  #5 exit=0 duration=0.39s
2026-10-29 14:03:12.441 FAIL  #6 exit=1 duration=0.02s
2026-10-29 14:41:50.007 SUMMARY result=failed exit=1 steps=7 commands=48 failed_commands=1 warnings=2 errors=1 duration=2318.5s
```

Levels: `INFO` `OK` `WARN` `ERROR` (what was shown), `STEP` (a pipeline step), `CMD`/`OUT`/`DONE`/`FAIL` (a command,
its output, its result - tied together by the command number `#n`), and a final `SUMMARY`. Colour codes and
progress-bar redraws are stripped from the log. Quick checks:

```bash
grep -E ' (FAIL|ERROR|WARN) ' /var/log/cccu/install.log
grep ' #6 ' /var/log/cccu/install.log        # everything about command 6
```

## Source layout

`build.sh`: installs the dependencies and builds everything. `packaging/`: application-menu entry and icon.

`gui/`: the Qt6 desktop version (`mainwindow` window, settings pages and live log; `main`).

`src/`: `bootstrap` ISO extraction and the built-in base package lists, `main` command line and run control, `menu` main menu and settings screens, `util` terminal UI, progress
and logging, `proc` command execution, `hostinstall` drive installation, `drive` partitioning, `installer`
container pipeline and running-system setup, `distro` family detection and per-distro commands, `incus` Incus
wrapper, `profiles` container profile, `sources` image/clone/export, `rootfs` system adaptation, `config` options,
`iso` / `iso_embed` ISO creation inside a container, `containers` duplicate and switch.

`src/remaster/`: the ISO engine (claudebackup remaster code) with its boot files and Calamares data; built as
`cccu-iso-engine` and carried inside `cccu`.

## FAQ

**Is the container a virtual machine?** No. It is an Incus system container that shares the host kernel.

**Is there a login screen?** No. The configured user is logged in automatically; there is no option to turn it off.

**Do I create a user for the container?** No - the container is a clone, so its users already exist and the
settings screen lists them. The tty host account can have a different name.

**Do I have to give a size?** Yes. There is no default; the settings screen suggests one from your image.

**Where are the logs?** `/var/log/cccu/` - see [Logging](#logging).

## Status and roadmap

Experimental. The Arch path is the primary target. The Debian, Fedora and openSUSE paths, sized/compressed/
immutable storage and the running-system mode are implemented but not yet field-tested - please report what
breaks.

Planned: Docker containers from a cloned system, bootstrapping a base for other distros without a ready-made
image, field-testing of the non-Arch paths, non-systemd inits.

---

<div align="center">

**[ Support Me ](https://www.paypal.com/paypalme/claudemods?country.x=GB&locale)** · [claudemodsreloaded.co.uk](https://www.claudemodsreloaded.co.uk)

</div>
