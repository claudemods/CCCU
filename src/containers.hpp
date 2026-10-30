#pragma once
#include "config.hpp"
#include "util.hpp"

#include <string>

namespace cccu {

// The container the host's "Container Plasma" session shows (/etc/cccu/active); empty = the one baked into
// the session script at install time.
std::string active_container();

// Prints the containers: name, state, and which one is active. plain = tab separated lines for the GUI.
Result list_containers(bool plain);

// `incus copy` of o.name to o.target (a full, independent clone with its own storage). With o.switch_now the
// copy becomes the active container.
Result duplicate_container(Options options);

// Makes o.name the container the host session shows: records it, autostarts it, starts it, and stops the
// previous one (unless o.keep_old). o.container_user = the account inside it (detected when empty).
Result switch_container(Options options);

// start / stop a container by name.
Result set_container_running(const std::string& name, bool running);

}  // namespace cccu
