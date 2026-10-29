# claudeincus-installer

Installs a small **Arch host** onto a drive, with **Incus** on it, and puts your cloned
**KDE desktop + apps in an Incus system container**.

```
sda1  EFI boot (FAT32, GRUB)
sda2  host system (ext4 by default)  -> SDDM + Incus
        └─ container "desktop"      -> your cloned Arch: KDE Plasma and apps
```

Wayland only: no X server is installed. Package installs are minimal: only packages missing from the package database are installed (checked with `pacman -T`, no network),
without a system upgrade (an already-installed package is never downloaded again).

**Display modes** (`--display`)

- `session` (default): the **host's SDDM** shows a login session called **Container Plasma**. Choosing it starts
  the container and runs its KDE Plasma fullscreen inside a kiosk compositor (`cage`) on the host's screen, with
  the Wayland, PipeWire and Pulse sockets proxied in and the GPU passed through. The host user must be in
  `incus-admin`; the installer creates the user (`--user NAME` / prompt; `--autologin` is optional and off by default) and sets that up. The host's login
  screen only appears once the first-boot service has created the container (console shows its progress).
- `native`: the container runs its own SDDM/Plasma Login Manager on VT1 (GPU, VTs, input, sound passed through);
  the host stays on text consoles.

## Build

Needs GCC 14+ (or Clang 17+) with C++23 and CMake 3.25+. No other libraries.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
sudo ./build/claudeincus-installer          # menu: install to a drive / container only / export
```

Optional: `sudo cmake --install build` puts it in `/usr/local/bin`. Rebuild from scratch with `rm -rf build` first.

## 1. Make the desktop image (on your Arch machine, once)

```bash
sudo ./build/claudeincus-installer --export desktop.tar.zst
```

## 2. Install (boot the Arch live ISO in UEFI mode, copy the binary + image over)

```bash
sudo ./claudeincus-installer install --drive /dev/sda --image desktop.tar.zst --user YOURUSER
```

What `install` does (same shape as the old `installerlinux` script):

1. Wipes `--drive`, GPT: p1 EFI 551 MiB (FAT32), p2 system. ext4 is the default; `--fs btrfs` creates the subvolumes `@ @home @root @srv @cache @tmp @log`
   (zstd compression, as in the old script). Without `--fs`, `install` asks which one you want.
   Refuses the drive holding `/`, mounted drives and swap; you must retype the device path (skipped by `-y`).
2. Unpacks the host system image into p2: `--host-image`, or automatically the live ISO's root image
   (`/run/archiso/bootmnt/arch/x86_64/airootfs.sfs`, `.erofs`, or `/run/arch/bootmnt/LiveOS/rootfs.img`).
   The image must contain a kernel in `/boot`. Or use `--clone-system` to rsync-clone the system you
   booted instead (kernel included; any desktop it had is switched off, so the clone becomes the tty host).
   The desktop image is always separate and is never used as the host.
3. Writes fstab (UUIDs), chroots, installs missing packages (incus, rsync, grub, efibootmgr, …),
   runs `mkinitcpio -P`, `grub-install`, `grub-mkconfig`.
4. Configures the host as tty-only: no display manager, DHCP on wired NICs, Incus enabled, VT1 left
   to the container (host login on tty2), asks you to set the root password.
5. Copies the desktop image and this program onto the host and enables `claudeincus-firstboot.service`.

**First boot of the new system:** the service creates the container from the staged image
(`journalctl -u claudeincus-firstboot -f` on tty2), sets `boot.autostart=true`, starts it, and the
container's login screen appears on VT1. Later boots start the container automatically.

## Container-only mode (machine already running Incus)

```bash
sudo ./claudeincus-installer desktop --image desktop.tar.zst --user YOURUSER
sudo ./claudeincus-installer desktop --clone-host        # clone the running machine instead
```

It installs missing tools/Incus (pacman, apt, dnf or zypper), sets subuid/subgid, runs
`incus admin init --minimal` on a fresh install, picks the storage pool, and creates the container with
the `claudeincus-desktop` profile (GPU, VTs, input, sound, host udev DB). If a desktop is already
running on that host the container is created but not started (the GPU/VT can only have one owner).

Add `--dry-run` to any command to print each step without changing anything.

## What is changed in the container's system

empty `fstab`, `mtab` → `/proc/self/mounts`, fresh `machine-id`, hostname, `hosts` · `CheckSpace` off,
mkinitcpio/dracut pacman hooks neutralised · host-only units masked (remount-fs, modules-load, audit,
incus, udevd — the host's `/run/udev` is mounted in instead) · NetworkManager kept, else systemd-networkd on `eth0` ·
display manager enabled + `graphical.target` · optional autologin.

## Source layout (`src/`)

`main` CLI · `hostinstall` live-ISO → drive install · `drive` partition/format/mount · `installer` container pipeline ·
`incus` CLI wrapper · `profiles` device profile · `sources` image/clone/export · `rootfs` system adaptation ·
`proc`/`util`/`config` helpers

## Unverified (never built or run with this code)

`plasmalogin.service` name and `/etc/plasmalogin.conf.d`; `/dev/tty0..7` passthrough next to Incus's console
handling (drop the tty lines in `src/profiles.cpp` if they clash); the first-boot creation on the
host (pool `default` is a `dir` pool on the system partition); GRUB on the live image's package set.
