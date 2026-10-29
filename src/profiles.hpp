#pragma once
#include "config.hpp"
#include "util.hpp"

namespace claudeincus::profiles {

// Incus profile used for the container in this display mode.
const char* name(Display display);

// Creates/updates it:
//   Session - GPU only; the host session adds the Wayland/PipeWire/Pulse sockets at login.
//   Native  - GPU, VTs, input, sound and the host udev DB for a display manager inside.
Result install(Display display);

}  // namespace claudeincus::profiles
