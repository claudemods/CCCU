#pragma once
#include "config.hpp"
#include "util.hpp"

#include <filesystem>
#include <string>

namespace cccu::rootfs {

// Display manager unit actually present in the tree ("" if none), honouring the user's choice.
std::string pick_display_manager(const std::filesystem::path& root, DisplayManager wanted);

// Turns a freshly copied Arch tree into something systemd can boot as an Incus container and
// enables the KDE display manager.
Result adapt(const std::filesystem::path& root, const Options& options);

// Configures a freshly installed host tree to run Incus. Session mode: the host's SDDM offers the
// "Container Plasma" session. Native mode: tty-only host, VT1 left to the container's display manager.
// `running` = the tree is the live system (/): its hostname, machine-id and network setup are left alone.
Result adapt_host(const std::filesystem::path& root, const Options& options, bool running = false);

}  // namespace cccu::rootfs
