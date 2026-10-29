#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>

#include <sys/ioctl.h>
#include <unistd.h>

#ifndef CCCU_VERSION
#define CCCU_VERSION "1.0"
#endif
#ifndef CCCU_DATE
#define CCCU_DATE "29-10-2026"
#endif

namespace cccu {
namespace {
using Clock = std::chrono::steady_clock;

std::ofstream g_log;
std::filesystem::path g_log_path;

// Progress state: which step of the pipeline we are in, and the live status line.
int g_total_steps = 0;
int g_step = 0;
std::string g_step_title;
Clock::time_point g_run_start = Clock::now();
Clock::time_point g_step_start = Clock::now();
bool g_at_line_start = true;      // the cursor is at the start of an empty line (safe to draw the status line)
bool g_status_visible = false;
unsigned g_spin = 0;
int g_percent = -1;               // progress of the running command, -1 = unknown
int g_percent_printed = -1;
std::string g_detail;             // text next to the percentage (bytes, speed, ETA)

// Log state: counters for the SUMMARY record, and the output line that is still being assembled.
int g_commands = 0;
int g_commands_failed = 0;
int g_warnings = 0;
int g_errors = 0;
int g_open_command = 0;
std::string g_partial_line;

// "2026-10-29 14:03:12.044"
std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm local {};
    ::localtime_r(&seconds, &local);
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%d %H:%M:%S", &local);
    char out[40];
    std::snprintf(out, sizeof out, "%s.%03lld", buffer, static_cast<long long>(millis));
    return out;
}

void log_write(std::string_view text) {
    if (!g_log) return;
    g_log.write(text.data(), static_cast<std::streamsize>(text.size()));
    g_log.flush();
}

std::string pad_level(std::string_view level) {
    std::string text(level);
    if (text.size() < 5) text.append(5 - text.size(), ' ');
    return text;
}

// Drops colour escapes and carriage returns (progress bars) from command output.
std::string clean_output(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0x1b && i + 1 < text.size() && text[i + 1] == '[') {
            i += 2;
            while (i < text.size() && !std::isalpha(static_cast<unsigned char>(text[i]))) ++i;
        } else if (c == '\r') {
            out.push_back('\n');
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

void flush_partial_line() {
    if (g_partial_line.empty()) return;
    log_write(timestamp() + " OUT   #" + std::to_string(g_open_command) + " | " + g_partial_line + "\n");
    g_partial_line.clear();
}

std::string clock_text(Clock::duration d) {
    const long long seconds = std::chrono::duration_cast<std::chrono::seconds>(d).count();
    char buffer[24];
    if (seconds >= 3600) std::snprintf(buffer, sizeof buffer, "%lld:%02lld:%02lld", seconds / 3600, (seconds / 60) % 60, seconds % 60);
    else std::snprintf(buffer, sizeof buffer, "%02lld:%02lld", seconds / 60, seconds % 60);
    return buffer;
}

std::size_t terminal_columns() {
    winsize size {};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col >= 40) return size.ws_col;
    return 80;
}

std::string bar(int done, int total, std::size_t width) {
    const double fraction = total > 0 ? std::min(1.0, static_cast<double>(done) / total) : 0.0;
    const std::size_t filled = static_cast<std::size_t>(fraction * static_cast<double>(width));
    std::string text;
    for (std::size_t i = 0; i < width; ++i) text += (i < filled) ? "█" : "░";
    return text;
}

void erase_status() {
    if (!g_status_visible) return;
    std::cout << "\r\033[2K" << std::flush;
    g_status_visible = false;
}

// "[ INFO ] message": a coloured tag, then the message.
void emit(const char* tag, const char* level, const char* tint, bool tint_message, std::string_view message) {
    erase_status();
    if (message.empty()) {
        std::cout << std::endl;
        g_at_line_start = true;
        return;
    }
    std::cout << paint(tint, tag) << ' ' << (tint_message ? paint(tint, message) : std::string(message)) << std::endl;
    g_at_line_start = true;
    log_event(level, message);
}
}  // namespace

bool color_enabled() {
    static const bool enabled = [] {
        if (!::isatty(STDOUT_FILENO) || std::getenv("NO_COLOR")) return false;
        const char* term = std::getenv("TERM");
        return !(term && std::string_view(term) == "dumb");
    }();
    return enabled;
}

std::string paint(const char* code, std::string_view text) {
    if (!color_enabled()) return std::string(text);
    return std::string(code) + std::string(text) + color::reset;
}

std::size_t visible_width(std::string_view text) {
    std::size_t width = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == 0x1b && i + 1 < text.size() && text[i + 1] == '[') {   // skip an ANSI escape
            i += 2;
            while (i < text.size() && !std::isalpha(static_cast<unsigned char>(text[i]))) ++i;
            continue;
        }
        if ((c & 0xC0) != 0x80) ++width;   // count characters, not UTF-8 continuation bytes
    }
    return width;
}

std::string fit(std::string_view text, std::size_t width) {
    const std::size_t w = visible_width(text);
    if (w <= width) return std::string(text) + std::string(width - w, ' ');
    // Too long: keep the first characters of a plain string and end with "...".
    const std::size_t dots = std::min<std::size_t>(3, width);
    const std::size_t keep = width - dots;
    std::string cut;
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const bool lead = (static_cast<unsigned char>(text[i]) & 0xC0) != 0x80;
        if (lead && count == keep) break;
        if (lead) ++count;
        cut.push_back(text[i]);
    }
    return cut + std::string(dots, '.');
}

void clear_screen() {
    if (color_enabled()) std::cout << "\033[2J\033[H" << std::flush;
}

void log_open(const std::filesystem::path& file, std::string_view command_line) {
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    g_log.open(file, std::ios::app);
    if (!g_log) return;
    g_log_path = file;
    g_run_start = Clock::now();

    char host[256] = "?";
    ::gethostname(host, sizeof host - 1);
    const char* user = std::getenv("SUDO_USER");
    const std::string rule(78, '=');
    log_write("\n" + rule + "\n");
    log_write(" CCCU v" CCCU_VERSION " " CCCU_DATE "  -  Claudemods Container Creator Unity\n");
    log_write(" started : " + timestamp() + "\n");
    log_write(" host    : " + std::string(host) + "\n");
    log_write(" user    : " + std::string(user ? user : "root") + "\n");
    if (!command_line.empty()) log_write(" command : " + std::string(command_line) + "\n");
    log_write(" log     : " + file.string() + "\n");
    log_write(rule + "\n");
    log_write("Format: <date> <time> LEVEL text   (CMD/OUT/DONE records carry the command number #n)\n");
}

void log_close(int exit_code) {
    if (!g_log) return;
    flush_partial_line();
    const double seconds = std::chrono::duration<double>(Clock::now() - g_run_start).count();
    char duration[32];
    std::snprintf(duration, sizeof duration, "%.1fs", seconds);
    log_write(timestamp() + " SUMMARY result=" + (exit_code == 0 ? "success" : "failed") + " exit=" + std::to_string(exit_code) +
              " steps=" + std::to_string(g_step) + " commands=" + std::to_string(g_commands) +
              " failed_commands=" + std::to_string(g_commands_failed) + " warnings=" + std::to_string(g_warnings) +
              " errors=" + std::to_string(g_errors) + " duration=" + duration + "\n");
}

void log_event(std::string_view level, std::string_view message) {
    if (level == "WARN") ++g_warnings;
    if (level == "ERROR") ++g_errors;
    if (!g_log) return;
    flush_partial_line();
    log_write(timestamp() + " " + pad_level(level) + " " + std::string(message) + "\n");
}

int log_command_begin(std::string_view command_line, bool dry_run) {
    const int number = ++g_commands;
    g_open_command = number;
    g_partial_line.clear();
    if (g_log) {
        flush_partial_line();
        log_write(timestamp() + " CMD   #" + std::to_string(number) + " " + std::string(command_line) + (dry_run ? "   (dry-run, skipped)" : "") + "\n");
    }
    return number;
}

void log_command_output(int number, std::string_view chunk) {
    if (!g_log) return;
    g_open_command = number;
    g_partial_line += clean_output(chunk);
    for (std::size_t newline; (newline = g_partial_line.find('\n')) != std::string::npos;) {
        const std::string line = g_partial_line.substr(0, newline);
        g_partial_line.erase(0, newline + 1);
        if (!line.empty()) log_write(timestamp() + " OUT   #" + std::to_string(number) + " | " + line + "\n");
    }
}

void log_command_end(int number, int exit_code, double seconds) {
    if (exit_code != 0) ++g_commands_failed;
    if (!g_log) return;
    g_open_command = number;
    flush_partial_line();
    char duration[32];
    std::snprintf(duration, sizeof duration, "%.2fs", seconds);
    log_write(timestamp() + (exit_code == 0 ? " DONE  #" : " FAIL  #") + std::to_string(number) + " exit=" + std::to_string(exit_code) +
              " duration=" + duration + "\n");
}

const std::filesystem::path& log_path() { return g_log_path; }

void info(std::string_view message) { emit("[ INFO ]", "INFO", color::cyan, false, message); }
void ok(std::string_view message) { emit("[  OK  ]", "OK", color::green, true, message); }
void warn(std::string_view message) { emit("[ WARN ]", "WARN", color::yellow, true, message); }
void error(std::string_view message) { emit("[ FAIL ]", "ERROR", color::red, true, message); }

void set_total_steps(int total) {
    g_total_steps = total;
    g_step = 0;
    g_run_start = Clock::now();
}

void step(std::string_view message) {
    erase_status();
    ++g_step;
    if (g_step > g_total_steps) g_total_steps = g_step;   // the estimate was short: the bar never shows 100% before the end
    g_step_title = std::string(message);
    g_step_start = Clock::now();
    g_percent = -1;
    g_percent_printed = -1;
    g_detail.clear();

    const std::string counter = "[" + std::to_string(g_step) + "/" + std::to_string(g_total_steps) + "]";
    const std::size_t columns = std::min<std::size_t>(terminal_columns(), 100);
    const std::string head = "── " + counter + " " + g_step_title + " ";
    const std::size_t used = visible_width(head);
    const std::string rule(used < columns ? columns - used : 0, '-');
    // Draw the rule with the box character that every Linux console font has.
    std::string line;
    for (std::size_t i = 0; i < rule.size(); ++i) line += "─";

    const int percent = static_cast<int>(100.0 * (g_step - 1) / g_total_steps);
    std::cout << '\n' << paint(color::green, head + line) << '\n';
    std::cout << "   " << paint(color::green, bar(g_step - 1, g_total_steps, 30)) << ' ' << percent << "%   elapsed "
              << clock_text(Clock::now() - g_run_start) << std::endl;
    g_at_line_start = true;
    log_event("STEP", counter + " " + g_step_title);
}

void status_tick() {
    if (!color_enabled() || g_step == 0 || !g_at_line_start) return;
    static const char frames[] = {'|', '/', '-', '\\'};
    const std::size_t columns = terminal_columns();
    const std::string tail = (g_percent >= 0 ? "  " + std::to_string(g_percent) + "%" + (g_detail.empty() ? std::string() : "  " + g_detail) : std::string()) + "  step " + clock_text(Clock::now() - g_step_start) + "  total " + clock_text(Clock::now() - g_run_start);
    const std::size_t room = columns > tail.size() + 8 ? columns - tail.size() - 8 : 10;
    std::cout << "\r\033[2K" << paint(color::cyan, std::string("[ ") + frames[g_spin++ % 4] + " ]") << ' '
              << fit(g_step_title, std::min(room, visible_width(g_step_title))) << paint(color::grey, tail) << std::flush;
    g_status_visible = true;
}

void console_write(std::string_view chunk) {
    if (chunk.empty()) return;
    erase_status();
    if (color_enabled()) std::cout << color::dark_green;
    std::cout.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    if (color_enabled()) std::cout << color::reset;
    std::cout.flush();
    g_at_line_start = chunk.back() == '\n';
    status_tick();
}

void status_finish() {
    erase_status();
}

void status_progress(int percent, std::string_view detail) {
    percent = std::min(percent, 100);
    g_percent = percent;
    g_detail = std::string(detail);
    if (percent < 0) return;
    const bool changed = percent != g_percent_printed;
    g_percent_printed = percent;
    if (changed && percent % 10 == 0) log_event("PROG", std::to_string(percent) + "%" + (detail.empty() ? "" : "  " + std::string(detail)));
    if (color_enabled()) {
        status_tick();   // redraws the live line with the percentage and the detail
    } else if (changed) {
        erase_status();
        std::cout << "    ... " << percent << "%" << (detail.empty() ? "" : "  " + std::string(detail)) << std::endl;
        g_at_line_start = true;
    }
}

void print_banner() {
    if (color_enabled()) std::cout << color::red;
    std::cout << R"BANNER(░█████╗░██╗░░░░░░█████╗░██║░░░██╗██████╗░███████╗███╗░░░███╗░█████╗░██████╗░░██████╗
██╔══██╗██║░░░░░██╔══██╗██║░░░██║██╔══██╗██╔════╝████╗░████║██╔══██╗██╔══██╗██╔════╝
██║░░╚═╝██║░░░░░███████║██║░░░██║██║░░██║█████╗░░██╔████╔██║██║░░██║██║░░██║╚█████╗░
██║░░██╗██║░░░░░██╔══██║██║░░░██║██║░░██║██╔══╝░░██║╚██╔╝██║██║░░██║██║░░██║░╚═══██╗
╚█████╔╝███████╗██║░░██║╚██████╔╝██████╔╝███████╗██║░╚═╝░██║╚█████╔╝██████╔╝██████╔╝
░╚════╝░╚══════╝╚═╝░░╚═╝░╚═════╝░╚═════╝░╚══════╝╚═╝░░░░░╚═╝░╚════╝░╚═════╝░╚═════╝░)BANNER";
    if (color_enabled()) std::cout << color::reset;
    std::cout << '\n'
              << paint(color::white, std::string("CCCU  -  Claudemods Container Creator Unity  v") + CCCU_VERSION + " " + CCCU_DATE) << '\n'
              << paint(color::grey, "Sailing The Seven Seas like Penguin's Eggs, Remastersys, Refracta, Systemback and father Knoppix!")
              << '\n'
              << std::endl;
    g_at_line_start = true;
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return std::string(text.substr(begin, end - begin));
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(current);
            current.clear();
        } else if (c != '\r') {
            current.push_back(c);
        }
    }
    if (!current.empty()) lines.push_back(current);
    return lines;
}

std::string ask(std::string_view question, std::string_view fallback) {
    erase_status();
    std::cout << paint(color::cyan, "  >") << ' ' << paint(color::white, question);
    if (!fallback.empty()) std::cout << ' ' << paint(color::grey, "[" + std::string(fallback) + "]");
    std::cout << paint(color::cyan, ": ") << std::flush;
    std::string answer;
    if (!std::getline(std::cin, answer)) return std::string(fallback);
    g_at_line_start = true;
    answer = trim(answer);
    return answer.empty() ? std::string(fallback) : answer;
}

bool confirm(std::string_view question, bool fallback) {
    const std::string answer = ask(std::string(question) + (fallback ? " (Y/n)" : " (y/N)"));
    if (answer.empty()) return fallback;
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(answer.front())));
    return c == 'y';
}

bool input_closed() { return std::cin.eof(); }

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

Result write_text(const std::filesystem::path& path, std::string_view content,
                  std::filesystem::perms mode) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return fail("cannot create " + path.parent_path().string() + ": " + ec.message());

    // A symlink at the destination (common in cloned roots) must not redirect the write
    // outside the target tree.
    std::filesystem::remove(path, ec);

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) return fail("cannot write " + path.string());
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!out) return fail("short write to " + path.string());
    }
    std::filesystem::permissions(path, mode, ec);
    if (ec) return fail("cannot chmod " + path.string() + ": " + ec.message());
    return {};
}

std::string human_bytes(unsigned long long bytes) {
    char buffer[32];
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gib >= 1.0) std::snprintf(buffer, sizeof buffer, "%.1f GiB", gib);
    else std::snprintf(buffer, sizeof buffer, "%.0f MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buffer;
}

bool valid_instance_name(std::string_view name) {
    if (name.empty() || name.size() > 63) return false;
    if (!std::isalpha(static_cast<unsigned char>(name.front()))) return false;
    if (name.back() == '-') return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') return false;
    }
    return true;
}

}  // namespace cccu
