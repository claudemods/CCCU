#pragma once
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace claudeincus {

using Result = std::expected<void, std::string>;

inline std::unexpected<std::string> fail(std::string message) {
    return std::unexpected<std::string>(std::move(message));
}

namespace color {
inline constexpr const char* red = "\033[1;31m";
inline constexpr const char* green = "\033[1;32m";              // what is happening
inline constexpr const char* dark_green = "\033[0;38;5;28m";    // what the commands print
inline constexpr const char* yellow = "\033[33m";
inline constexpr const char* cyan = "\033[38;2;0;255;255m";
inline constexpr const char* reset = "\033[0m";
}  // namespace color

// The red claudemods banner.
void print_banner();

void info(std::string_view message);
void ok(std::string_view message);
void warn(std::string_view message);
void error(std::string_view message);
void step(std::string_view message);

// File log: everything printed through info/ok/warn/error/step, every command run and its output.
void log_open(const std::filesystem::path& file);   // appends; creates parent directories
void log_raw(std::string_view text);
const std::filesystem::path& log_path();            // empty when logging is off

std::string trim(std::string_view text);
std::vector<std::string> split_lines(std::string_view text);

// Prompts. Both return the fallback on EOF / empty input.
std::string ask(std::string_view question, std::string_view fallback = {});
bool confirm(std::string_view question, bool fallback);

std::string read_text(const std::filesystem::path& path);
Result write_text(const std::filesystem::path& path, std::string_view content,
                  std::filesystem::perms mode = std::filesystem::perms::owner_read |
                                                std::filesystem::perms::owner_write |
                                                std::filesystem::perms::group_read |
                                                std::filesystem::perms::others_read);

// "4.2 GiB", "850 MiB".
std::string human_bytes(unsigned long long bytes);

// Incus instance names: letters, digits and '-', starting with a letter, max 63 chars.
bool valid_instance_name(std::string_view name);

}  // namespace claudeincus
