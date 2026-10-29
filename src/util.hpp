#pragma once
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cccu {

using Result = std::expected<void, std::string>;

inline std::unexpected<std::string> fail(std::string message) {
    return std::unexpected<std::string>(std::move(message));
}

namespace color {
inline constexpr const char* red = "\033[1;31m";
inline constexpr const char* green = "\033[1;32m";              // what is happening
inline constexpr const char* dark_green = "\033[0;38;5;28m";    // what the commands print
inline constexpr const char* yellow = "\033[1;33m";
inline constexpr const char* cyan = "\033[1;36m";
inline constexpr const char* white = "\033[1;97m";
inline constexpr const char* grey = "\033[0;90m";
inline constexpr const char* bold = "\033[1m";
inline constexpr const char* badge_ok = "\033[1;30;42m";        // green box: black on green
inline constexpr const char* badge_bad = "\033[1;97;41m";       // red box: white on red
inline constexpr const char* reset = "\033[0m";
}  // namespace color

// ---- terminal ---------------------------------------------------------------------------------

// True when stdout is a terminal and NO_COLOR is not set. Everything below degrades to plain text
// (no escapes, no live status line) when it is false, so logs and pipes stay clean.
bool color_enabled();
// `code` + text + reset, or just the text when colours are off.
std::string paint(const char* code, std::string_view text);
// Number of columns the text takes on screen (ANSI escapes ignored, UTF-8 counted per character).
std::size_t visible_width(std::string_view text);
// Pads / cuts text to exactly `width` columns.
std::string fit(std::string_view text, std::size_t width);
void clear_screen();

// The red claudemods banner + the CCCU title line.
void print_banner();

void info(std::string_view message);
void ok(std::string_view message);
void warn(std::string_view message);
void error(std::string_view message);

// ---- progress ---------------------------------------------------------------------------------

// A pipeline is a series of steps. step() prints a header with "[n/total]" and an overall progress
// bar; while a command runs, a live line (spinner, step name, elapsed time) is kept at the bottom
// of the screen and refreshed even when the command prints nothing.
void set_total_steps(int total);
void step(std::string_view message);
// Live status line, driven by run() (see proc.cpp).
void status_tick();                                  // redraw spinner + elapsed time
void console_write(std::string_view chunk);          // command output: erases the live line, writes, redraws it
void status_finish();                                // erase the live line for good
// Percent of the running command (0-100, -1 = unknown): shown in the live line on a terminal; without a
// terminal (the GUI reads this) a line "    ... 42%" is printed whenever it changes.
// `detail` is shown next to it, e.g. "1.8 GiB / 4.2 GiB  96 MiB/s  ETA 00:25".
void status_progress(int percent, std::string_view detail = {});

// ---- log file ---------------------------------------------------------------------------------
//
// One record per line:  2026-10-29 14:03:12.044 LEVEL  text
//   INFO/OK/WARN/ERROR  what info()/ok()/warn()/error() showed on screen
//   STEP                a pipeline step started ("[3/10] Partitioning ...")
//   CMD   #7            a command was started (full argv; "dry-run" when skipped)
//   OUT   #7 | ...      one line of its output
//   DONE  #7            finished: exit code and duration (FAIL when the exit code is not 0)
// A session header at the start names version, host, user and the command line; a SUMMARY record at
// the end gives the result, counts and total duration.
void log_open(const std::filesystem::path& file, std::string_view command_line = {});   // appends; creates parent directories
void log_close(int exit_code);                       // writes the SUMMARY record
void log_event(std::string_view level, std::string_view message);
int log_command_begin(std::string_view command_line, bool dry_run);           // returns the command number
void log_command_output(int number, std::string_view chunk);                  // may be split anywhere
void log_command_end(int number, int exit_code, double seconds);
const std::filesystem::path& log_path();            // empty when logging is off

std::string trim(std::string_view text);
std::vector<std::string> split_lines(std::string_view text);

// Prompts. Both return the fallback on EOF / empty input; input_closed() tells EOF apart.
std::string ask(std::string_view question, std::string_view fallback = {});
bool confirm(std::string_view question, bool fallback);
bool input_closed();

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

}  // namespace cccu
