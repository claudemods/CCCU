#include "util.hpp"

#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>

namespace claudeincus {
namespace {
std::ofstream g_log;
std::filesystem::path g_log_path;

std::string timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local {};
    ::localtime_r(&now, &local);
    char buffer[16];
    std::strftime(buffer, sizeof buffer, "%H:%M:%S", &local);
    return buffer;
}
}  // namespace

void log_open(const std::filesystem::path& file) {
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    g_log.open(file, std::ios::app);
    if (g_log) {
        g_log_path = file;
        g_log << "\n===== " << timestamp() << " claudeincus-installer started =====" << std::endl;
    }
}

void log_raw(std::string_view text) {
    if (!g_log) return;
    g_log.write(text.data(), static_cast<std::streamsize>(text.size()));
    g_log.flush();
}

const std::filesystem::path& log_path() { return g_log_path; }

namespace {
void emit(const char* tint, std::string_view message) {
    std::cout << tint << message << color::reset << std::endl;
    log_raw("[" + timestamp() + "] " + std::string(message) + "\n");
}
}  // namespace

// What is happening: green. What the commands print: dark green (see run() in proc.cpp).
void info(std::string_view message) { emit(color::green, message); }
void ok(std::string_view message) { emit(color::green, message); }
void warn(std::string_view message) { emit(color::yellow, message); }
void error(std::string_view message) { emit(color::red, message); }
void step(std::string_view message) {
    std::cout << '\n' << color::green << "==> " << message << color::reset << std::endl;
    log_raw("\n[" + timestamp() + "] ==> " + std::string(message) + "\n");
}

void print_banner() {
    std::cout << color::red << R"BANNER(░█████╗░██╗░░░░░░█████╗░██║░░░██╗██████╗░███████╗███╗░░░███╗░█████╗░██████╗░░██████╗
██╔══██╗██║░░░░░██╔══██╗██║░░░██║██╔══██╗██╔════╝████╗░████║██╔══██╗██╔══██╗██╔════╝
██║░░╚═╝██║░░░░░███████║██║░░░██║██║░░██║█████╗░░██╔████╔██║██║░░██║██║░░██║╚█████╗░
██║░░██╗██║░░░░░██╔══██║██║░░░██║██║░░██║██╔══╝░░██║╚██╔╝██║██║░░██║██║░░██║░╚═══██╗
╚█████╔╝███████╗██║░░██║╚██████╔╝██████╔╝███████╗██║░╚═╝░██║╚█████╔╝██████╔╝██████╔╝
░╚════╝░╚══════╝╚═╝░░╚═╝░╚═════╝░╚═════╝░╚══════╝╚═╝░░░░░╚═╝░╚════╝░╚═════╝░╚═════╝░
claudemods container creator unity v1.0 29-10-2026
Sailing The Seven Seas like Penguin's Eggs Remastersys, Refracta, Systemback and father Knoppix!)BANNER"
              << color::reset << std::endl;
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
    std::cout << color::green << question;
    if (!fallback.empty()) std::cout << " [" << fallback << "]";
    std::cout << ": " << color::reset << std::flush;
    std::string answer;
    if (!std::getline(std::cin, answer)) return std::string(fallback);
    answer = trim(answer);
    return answer.empty() ? std::string(fallback) : answer;
}

bool confirm(std::string_view question, bool fallback) {
    const std::string answer = ask(std::string(question) + (fallback ? " (Y/n)" : " (y/N)"));
    if (answer.empty()) return fallback;
    const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(answer.front())));
    return c == 'y';
}

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

}  // namespace claudeincus
