#pragma once
#include "config.hpp"
#include "util.hpp"

namespace claudeincus {

// The one job: install what is missing, then clone this Arch system into an Incus system
// container with the KDE display manager (SDDM / Plasma Login) running inside it.
Result create_container(Options options);

// Run on the source Arch machine: writes it as a tar image to install on another machine.
Result export_image(const Options& options);

}  // namespace claudeincus
