#pragma once
#include "config.hpp"
#include "util.hpp"

namespace claudeincus {

// Run from an Arch live environment. Wipes options.drive and installs:
//   p1 = EFI boot, p2 = tty host system (from options.host_image or the live rootfs) with GRUB + Incus.
// The cloned desktop (options.image) is staged on the host and turned into the Incus container on
// the host's first boot, where the display manager (SDDM / Plasma Login) takes VT1.
Result install_to_drive(Options options);

}  // namespace claudeincus
