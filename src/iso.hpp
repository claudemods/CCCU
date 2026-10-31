#pragma once
#include "config.hpp"
#include "util.hpp"

#include <string>
#include <vector>

namespace cccu {

// Where ISOs are saved unless told otherwise: Downloads in the home folder of the person who started cccu (sudo /
// pkexec user), else the current folder.
std::string default_iso_folder();

// Builds a bootable live ISO of the system inside an Incus container (o.name), with the claudebackup-linux
// engine running INSIDE the container: it installs the ISO tools and a kernel there when they are missing,
// builds the ISO, and the finished file lands in the host folder o.iso_out. Arch, Debian, Fedora and openSUSE
// families - the ones CCCU can clone.
Result create_iso(Options options);

// Re-runs this program as a systemd service on VT1, the way the first-boot service shows its progress:
// same banner, progress bar and live status line on the real console. `args` is the command line without
// --on-console.
Result start_on_console(const std::vector<std::string>& args);

// Sets the build up and leaves it for the next boot, the way `cccu install` leaves the container for the first
// boot: a one-shot service (cccu-iso-boot) runs `cccu iso` on VT1 with the progress display, the login screen
// waits until it is done, then it removes itself. `args` is the command line without --on-reboot.
Result schedule_iso_for_boot(const Options& options, const std::vector<std::string>& args);

// Writes an ISO to a whole drive (dd). Refuses a drive the running system lives on.
Result write_iso_to_usb(const Options& options, const std::string& drive);

}  // namespace cccu
