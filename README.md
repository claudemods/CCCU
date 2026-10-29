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

  ## [ Guide ](#-usage)

  ## [ Support Me ](https://www.paypal.com/paypalme/claudemods?country.x=GB&locale)

</div>

# 🐧 Claudemods Container Creator Unity
🌊 *Sailing The Seven Seas like Penguin's Eggs, Remastersys, Refracta, Systemback and father Knoppix!*

> **Create Incus system containers from cloned Linux systems** — clone the Linux you already run, put it in a
> container with its own sized (and optionally compressed and immutable) storage, and boot straight into its
> KDE desktop on a tiny tty host. 

---

## ✨ What is it?

You have a Linux system you love — your **desktop, your apps, your settings**. This tool:

1. 📸 **Clones** it (a running system, or an image made with `--export`),
2. 📦 **Turns it into an Incus system container** (a full systemd system that shares the host kernel, **not** a VM),
3. 🖥️ **Sets up a small tty host** under it — on a fresh drive, or on the machine you are running right now,
4. 🔑 **Logs you in automatically** into the container's KDE Plasma desktop (or shows a login screen, your choice).

```
 drive / running system
  ├─ 🥾 EFI boot (FAT32, GRUB)              (drive install)
  └─ 🐧 tty host  ── Incus ── SDDM + cage
          └─ 📦 container "desktop"  ← your cloned Linux: KDE Plasma and all your apps
                 └─ own btrfs storage: sized by you, compressed, optionally immutable 🔒
```

---

## 🚀 Features

| | |
|---|---|
| 🎨 **Interactive installer** | red claudemods banner, green status text, dark green command output — the commands themselves stay hidden |
| 🧭 **Normal-installer questions** | hostname, username, auto-login, filesystem, container name… no silent guesses |
| 📏 **Container size you choose** | **no default** — the installer hints a size from the image you give it, then asks |
| 🗜️ **Compression choice** | zstd `15 / 9 / 3`, zlib `9`, lzo or none — applied to the container's btrfs storage (and a btrfs host) |
| 🔒 **Optional immutable container** | system mounted **read-only**, `/etc /var /home /root /opt /srv /usr/local` stay writable |
| 🔑 **Auto-login by default** | straight to the desktop; one question to turn it off |
| 🖥️ **Running-system mode** | set up the container on the machine you are on (a tty system) — the host's dependencies are installed for you |
| 💾 **Drive mode** | wipes a drive: GPT, EFI + system partition (ext4 or btrfs), GRUB, Incus, first-boot container creation |
| 📤 **Export** | write any running Linux as a `.tar.zst` image to clone elsewhere |
| 🌍 **Multi-distro hosts** | Arch, Debian and Fedora families and openSUSE — package manager, initramfs and GRUB picked automatically |
| 📜 **Everything logged** | every command and its output is kept in `/var/log/claudeincus/` |

---

## 🐧 Supported systems

**20 systems are supported** (7 more are recognised and refused with a reason). The installer needs **systemd** inside the tty host and inside the container (units, logind, the first-boot service).

| Family | Systems | Package manager | Status |
|---|---|---|---|
| 🏹 **Arch** | Arch, CachyOS, EndeavourOS, Garuda, ArcoLinux, Manjaro | `pacman` | ✅ primary target |
| 🍥 **Debian** | Debian, Ubuntu, Kubuntu, KDE neon, Linux Mint, Pop!_OS, MX Linux¹ | `apt` | 🧪 implemented, needs field testing |
| 🎩 **Fedora** | Fedora, CentOS, AlmaLinux, Rocky Linux, RHEL, Nobara² | `dnf` | 🧪 implemented, needs field testing |
| 🦎 **openSUSE** | Leap, Tumbleweed | `zypper` | 🧪 implemented, needs field testing |
| ⛔ **Not supported** | NixOS, Gentoo, Slackware, Void, Devuan, KDE Linux, Artix | — | no systemd, or no installable package tree |

¹ MX Linux only when it boots with systemd (it often defaults to sysvinit). ² Incus comes from Fedora, EPEL or COPR depending on the release.

The installer **detects** the system from `/etc/os-release` and refuses an unsupported one with the reason, before it touches your drive.

---

## 🛠️ Build

Needs GCC 14+ (or Clang 17+) with **C++23** and CMake 3.25+. No other libraries.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo ./build/claudeincus-installer
```

---

## 📖 Usage

```bash
sudo ./build/claudeincus-installer
```

```
1. Install to a drive (wipe it: EFI boot + tty host + desktop container)
2. Set up the container on this running system (installs what the host needs)
3. Export this machine as a desktop image
```

### 1️⃣ Export your system (once, on the machine you want to clone)

```bash
sudo ./build/claudeincus-installer --export desktop.tar.zst
```

### 2️⃣ Install to a drive (boot an Arch live ISO in UEFI mode)

Menu option **1**, or:

```bash
sudo ./claudeincus-installer install --drive /dev/sda --image desktop.tar.zst --user YOURUSER --size 60 --compress zstd:15
```

You are asked, like a normal installer: **drive → image → tty host source → hostname → username → auto-login → filesystem → container name → container size → compression → immutable**.

### 3️⃣ Set up from a running system (a tty system)

Menu option **2**. Your current system becomes the container host:

* 📦 installs what the host needs with **its own package manager** — Incus, `btrfs-progs`, SDDM, `cage`, GPU and audio packages (installed ones are skipped),
* 👤 creates the desktop user and gives it Incus access,
* 🔧 configures the Container Plasma session (and auto-login),
* 🧊 creates the container from an image, **or by cloning this running system** (`host`).

> ℹ️ SDDM becomes the display manager at the next boot — reboot after the setup.

### 🧰 Options

| Option | |
|---|---|
| `--drive <dev>` | drive to wipe (install mode) |
| `--image <file>` | system image (`.tar` `.tar.zst` `.tar.xz` `.tar.gz`, squashfs, erofs) |
| `--clone-host` | clone the running machine instead of an image |
| `--hostname <name>` | hostname of the new tty system |
| `--user <name>` | desktop user (created if missing, added to `incus-admin`) |
| `--autologin` / `--no-autologin` | auto-login is the **default** |
| `--size <N>[GiB]` | container disk size — there is **no default** |
| `--compress <level>` | `zstd:15` `zstd:9` `zstd:3` `zlib:9` `lzo` `none` |
| `--immutable` | mount the container's system read-only |
| `--fs ext4\|btrfs` | host system filesystem |
| `--keep-kernel` `--keep-caches` `--no-start` `--dry-run` `-y` | see `--help` |

---

## 🧊 Container storage

### 📏 Size

The container gets **its own btrfs storage pool** (a loop-backed pool, so the size is really enforced). The installer reads your image and tells you:

```
Your image is 4.2 GiB (zstd-compressed tar); unpacked it needs roughly 12.6 GiB.
Leave room for updates and new apps: about 19 GiB or more is a sensible size.
```

…and then asks. Nothing is pre-selected.

### 🗜️ Compression

Applied as btrfs mount options on the container's pool (and on the host when it is btrfs).

| Choice | |
|---|---|
| `zstd:15` | the strongest zstd **btrfs** supports ⭐ |
| `zstd:9`, `zstd:3` | balanced / fast |
| `zlib:9`, `lzo` | alternatives |
| `none` | no compression |

> ⚠️ **btrfs supports zstd levels 1–15 only.** Level 22 exists only for zstd *archives* (like the exported `.tar.zst`), not for btrfs mounts — so the strongest choice is `zstd:15`.

### 🔒 Immutable

With `--immutable` the container's root disk is read-only. `/etc`, `/var`, `/home`, `/root`, `/opt`, `/srv` and `/usr/local` live on their own volumes and stay writable; `/tmp` is a tmpfs. To update the system:

```bash
incus config device set desktop root readonly=false   # then restart the container, update, and set it back to true
```

---

## 🔎 What happens to the cloned system

empty `fstab`, `mtab` → `/proc/self/mounts`, fresh `machine-id`, hostname, `hosts` · `CheckSpace` off, initramfs hooks neutralised · host-only units masked (remount-fs, modules-load, audit, incus, …) · NetworkManager kept, else systemd-networkd on `eth0` · the session sockets (Wayland, PipeWire, Pulse) are proxied in at login.

## 🪵 Logs

| File | |
|---|---|
| `/var/log/claudeincus/install.log` | the drive install |
| `/var/log/claudeincus/container.log` | container creation + diagnostics |
| `/var/log/claudeincus/export.log` | exports |

Every command and its output is kept in the log; on screen the **commands are hidden**, what they print is **dark green** and what is happening is **green**.

## 🗂️ Source layout (`src/`)

`main` CLI + questions · `hostinstall` drive install · `drive` partitioning · `installer` container pipeline + running-system setup · `distro` family detection and per-distro commands · `incus` Incus wrapper · `profiles` container profile · `sources` image/clone/export · `rootfs` system adaptation · `proc` / `util` / `config` helpers

## ⚠️ Status

🧪 **Experimental.** The Arch path is the primary target. The Debian, Fedora and openSUSE paths, the sized/compressed/immutable container storage and the running-system mode are implemented but **not yet field-tested** — please report what breaks.

🐳 **Docker**: creating Docker containers from a cloned system is **planned**, not implemented yet; today the output is an **Incus system container**.

---

<div align="center">

🌊 *Sailing The Seven Seas like Penguin's Eggs, Remastersys, Refracta, Systemback and father Knoppix!* 🌊

**[ Support Me ](https://www.paypal.com/paypalme/claudemods?country.x=GB&locale)** · 🌐 [claudemodsreloaded.co.uk](https://www.claudemodsreloaded.co.uk)

</div>
