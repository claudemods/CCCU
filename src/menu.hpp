#pragma once
#include "config.hpp"

#include <optional>
#include <string>

namespace cccu::menu {

enum class Mode { Install, Running, Export };

// Which answers were already given on the command line (so the menu does not override them).
struct Flags {
    bool fs_given = false;
    bool compress_given = false;
};

// The main menu. nullopt = quit (or the input closed).
std::optional<Mode> choose_mode();

// The settings screen of a mode: every setting is a row with a red NOT SET / green SET box. The user
// edits rows by number and presses S to start. true = start, false = cancelled.
bool settings(Mode mode, Options& o, Flags& flags);

// "40", "40G", "40GB", "40GiB" -> "40GiB"; "" when it is not a size.
std::string normalize_size(std::string text);
bool valid_hostname(const std::string& name);
bool valid_username(const std::string& name);

}  // namespace cccu::menu
