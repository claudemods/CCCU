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

# CCCU - Claudemods Container Creator Unity

> *Sailing The Seven Seas like Penguin's Eggs, Remastersys, Refracta, Systemback and father Knoppix!*

Clone the Linux system you already run into an **Incus system container** with its own sized (and optionally
compressed and immutable) storage, and boot straight into its KDE Plasma desktop on a minimal tty host.

## Contents

1. [Overview](#overview)
2. [Quick start](#quick-start)
3. [The interface](#the-interface)
4. [Build](#build)
5. [Desktop version (Qt6)](#desktop-version-qt6)
6. [Usage](#usage)
7. [Container storage](#container-storage)
8. [Supported systems](#supported-systems)
9. [Logging](#logging)
10. [Source layout](#source-layout)
11. [FAQ](#faq)
12. [Status and roadmap](#status-and-roadmap)

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
# 0. build and install (dependencies are installed for you)
bash build.sh --install

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

While an installation runs, every step prints a header with a counter (`[3/12]`), an overall progress bar and
the elapsed time. A live status line (spinner, step time, total time) stays at the bottom and keeps moving even
while a long command is silent. Messages carry level tags - `[ INFO ]`, `[  OK  ]`, `[ WARN ]`, `[ FAIL ]` -
and what the commands print is shown in dark green. Colour and the live line switch off automatically when the
output is not a terminal or `NO_COLOR` is set.

**First boot after an installation.** The `cccu-firstboot` service runs on a real terminal (VT1), so the same
progress display appears live while the container is created; the login console returns when it finishes. The
output is also available with `journalctl -u cccu-firstboot -f`.

## Build

Requires GCC 14+ (or Clang 17+) with C++23 and CMake 3.25+. The terminal tool needs no other libraries; the Qt6
desktop version additionally needs Qt 6 Widgets.

**One command (recommended).** `build.sh` detects your system (Arch, Debian/Ubuntu, Fedora or openSUSE family),
installs the build and runtime dependencies with its package manager, and builds both programs into `build/`:

```bash
bash build.sh                 # dependencies + cccu + cccu-gui
bash build.sh --install       # ... and install them (adds CCCU to the application menu)
```

| Option | |
|---|---|
| `--no-gui` | terminal tool only (no Qt6 needed) |
| `--deps-only` | only install the dependencies |
| `--no-deps` | build without touching the package manager |
| `--clean` | remove `build/` first |
| `-j <N>` | parallel jobs (default: all cores) |

It checks for a C++23 compiler (GCC 14+ or Clang 17+) and stops with a clear message if the system's is too old.
The dependencies are the compiler, CMake 3.25+, `rsync`, `tar`, `zstd`, `squashfs-tools`, `parted`, `dosfstools`,
`e2fsprogs`, `btrfs-progs`, and for the GUI Qt 6 Widgets and polkit. Incus itself is installed later by CCCU
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

`cccu-gui` offers the same three modes in a window. Every setting has the same status box as the terminal menu -
green `SET`, red `NOT SET`, grey `OPTIONAL` - and **Start** unlocks only when nothing required is red. A progress
bar follows the steps, and the log shows the installer output live (errors red, warnings amber).

* It is a front-end: it runs the `cccu` tool (found next to `cccu-gui`, or in `PATH`) with your settings, so both
  versions behave identically and write the same logs.
* It is started by clicking - no terminal. It needs root (it partitions drives and installs packages), so on
  start it asks for your password with the desktop's own authentication dialog (polkit / `pkexec`) and reopens
  with the rights it needs. If that is not available the window opens but Start stays disabled and says why.
* Before a drive install it asks you to type the device path to confirm the wipe.
* The user and root passwords are passed to the tool through its environment and set with `chpasswd`; they never
  appear on a command line or in the log.
* **Dry run** shows what would happen without changing anything. **Cancel** stops the running operation.

## Usage

| Menu entry | What it does |
|---|---|
| 1. Install to a drive | Wipes a drive: GPT, EFI + system partition (ext4 or btrfs), GRUB, Incus, and creates the container on the first boot |
| 2. Set up on this system | Installs what the host needs (Incus, `btrfs-progs`, SDDM, `cage`, GPU and audio packages) with the system's own package manager, creates the user and the container. Reboot afterwards: SDDM becomes the display manager |
| 3. Export this machine | Writes the running system as a `.tar.zst` image to clone elsewhere |

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

One record per line - `date time LEVEL text`:

```
============================================================================
 CCCU 3.0.0  -  Claudemods Container Creator Unity
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

`src/`: `main` command line and run control, `menu` main menu and settings screens, `util` terminal UI, progress
and logging, `proc` command execution, `hostinstall` drive installation, `drive` partitioning, `installer`
container pipeline and running-system setup, `distro` family detection and per-distro commands, `incus` Incus
wrapper, `profiles` container profile, `sources` image/clone/export, `rootfs` system adaptation, `config` options.

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

*Sailing The Seven Seas like Penguin's Eggs, Remastersys, Refracta, Systemback and father Knoppix!*

**[ Support Me ](https://www.paypal.com/paypalme/claudemods?country.x=GB&locale)** · [claudemodsreloaded.co.uk](https://www.claudemodsreloaded.co.uk)

</div>
