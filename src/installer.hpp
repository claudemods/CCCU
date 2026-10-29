#pragma once
#include "config.hpp"
#include "util.hpp"

namespace claudeincus {

// The one job: install what is missing, then clone this Arch system into an Incus system
// container with the KDE display manager (SDDM / Plasma Login) running inside it.
Result create_container(Options options);

// Set up the container on the system that is running now (a tty system): installs what the host needs to
// run it (Incus, SDDM + cage for the session, GPU and audio packages, ...) with the host's own package
// manager, configures the host (session, automatic login), then creates the container.
Result setup_running_system(Options options);

// Run on the source Arch machine: writes it as a tar image to install on another machine.
Result export_image(const Options& options);

}  // namespace claudeincus
