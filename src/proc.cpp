#include "proc.hpp"

#include "util.hpp"

#include <cerrno>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace cccu {
namespace {
bool g_dry_run = false;

std::string join(const std::vector<std::string>& argv) {
    std::string line;
    for (const auto& arg : argv) {
        if (!line.empty()) line += ' ';
        line += arg;
    }
    return line;
}
}  // namespace

void set_dry_run(bool enabled) { g_dry_run = enabled; }
bool dry_run() { return g_dry_run; }
bool is_root() { return ::geteuid() == 0; }

RunResult run(const std::vector<std::string>& argv, const RunOptions& options) {
    RunResult result;
    if (argv.empty()) return result;

    const std::string command_line = join(argv);
    const bool stream = !options.capture && !options.interactive;  // we relay output to screen + log

    // The command itself is not shown on screen (only in the log); what it prints is dark green.
    const bool skipped = g_dry_run && options.mutating;
    const int number = log_command_begin(command_line, skipped);
    const auto started = std::chrono::steady_clock::now();
    const auto seconds = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count(); };
    if (skipped) {
        result.code = 0;
        log_command_end(number, 0, 0.0);
        return result;
    }

    int pipefd[2] = {-1, -1};
    if ((options.capture || stream) && ::pipe(pipefd) != 0) {
        log_command_end(number, -1, seconds());
        return result;
    }

    // Streamed output is tinted dark green by console_write(); a command that owns the terminal
    // (interactive) is tinted here instead.
    struct DarkGreen {
        bool on;
        explicit DarkGreen(bool enable) : on(enable && color_enabled()) {
            if (on) std::cout << color::dark_green << std::flush;
        }
        ~DarkGreen() {
            if (on) std::cout << color::reset << std::flush;
        }
    } dark_green(options.interactive);

    std::cout.flush();
    const pid_t pid = ::fork();
    if (pid < 0) {
        if (options.capture || stream) {
            ::close(pipefd[0]);
            ::close(pipefd[1]);
        }
        log_command_end(number, -1, seconds());
        return result;
    }

    if (pid == 0) {
        if (options.capture || stream) {
            ::close(pipefd[0]);
            ::dup2(pipefd[1], STDOUT_FILENO);
            if (stream) ::dup2(pipefd[1], STDERR_FILENO);
            ::close(pipefd[1]);
        }
        if (options.capture && options.quiet) {  // silent probes (e.g. polling a booting container)
            const int null_fd = ::open("/dev/null", O_WRONLY);
            if (null_fd >= 0) {
                ::dup2(null_fd, STDERR_FILENO);
                ::close(null_fd);
            }
        }
        if (!options.stdin_file.empty()) {
            const int fd = ::open(options.stdin_file.c_str(), O_RDONLY);
            if (fd < 0) ::_exit(126);
            ::dup2(fd, STDIN_FILENO);
            ::close(fd);
        }
        std::vector<char*> args;
        args.reserve(argv.size() + 1);
        for (const auto& arg : argv) args.push_back(const_cast<char*>(arg.c_str()));
        args.push_back(nullptr);
        ::execvp(args[0], args.data());
        ::_exit(127);
    }

    if (options.capture || stream) {
        ::close(pipefd[1]);
        char buffer[4096];
        auto last_probe = std::chrono::steady_clock::now();
        // Bytes the command has read so far, as a percentage of options.progress_total.
        const auto probe_progress = [&] {
            if (options.progress_total == 0) return;
            const auto now = std::chrono::steady_clock::now();
            if (now - last_probe < std::chrono::milliseconds(400)) return;
            last_probe = now;
            std::ifstream io("/proc/" + std::to_string(pid) + "/io");
            for (std::string line; std::getline(io, line);) {
                if (line.rfind("rchar:", 0) != 0) continue;
                const unsigned long long read = std::strtoull(line.c_str() + 6, nullptr, 10);
                status_progress(static_cast<int>(std::min<unsigned long long>(99, read * 100 / options.progress_total)));
                break;
            }
        };
        for (;;) {
            // Wake up every 200 ms even when the command is silent, so the live status line
            // (spinner + elapsed time) keeps moving during long steps.
            pollfd watch {pipefd[0], POLLIN, 0};
            const int ready = ::poll(&watch, 1, 200);
            probe_progress();
            if (ready == 0) {
                if (stream) status_tick();
                continue;
            }
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) break;

            const ssize_t n = ::read(pipefd[0], buffer, sizeof buffer);
            if (n > 0) {
                const std::string_view chunk(buffer, static_cast<std::size_t>(n));
                if (options.capture) {
                    result.out.append(chunk);
                    if (!options.quiet) log_command_output(number, chunk);   // silent probes stay out of the log
                } else {
                    console_write(chunk);
                    log_command_output(number, chunk);
                }
            } else if (n < 0 && errno == EINTR) {
                continue;
            } else {
                break;
            }
        }
        ::close(pipefd[0]);
        if (stream) status_finish();
    }

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            log_command_end(number, -1, seconds());
            return result;
        }
    }
    if (WIFEXITED(status)) {
        result.code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.code = 128 + WTERMSIG(status);
    }
    log_command_end(number, result.code, seconds());
    return result;
}

std::string query(const std::vector<std::string>& argv) {
    RunOptions options;
    options.mutating = false;
    options.capture = true;
    options.quiet = true;
    const RunResult result = run(argv, options);
    return result.ok() ? trim(result.out) : std::string{};
}

namespace {
std::string clock_mmss(double seconds) {
    const long s = static_cast<long>(seconds);
    char buffer[24];
    if (s >= 3600) std::snprintf(buffer, sizeof buffer, "%ld:%02ld:%02ld", s / 3600, (s / 60) % 60, s % 60);
    else std::snprintf(buffer, sizeof buffer, "%02ld:%02ld", s / 60, s % 60);
    return buffer;
}
std::string rate_text(double bytes_per_second) {
    char buffer[32];
    if (bytes_per_second >= 1024.0 * 1024.0 * 1024.0) std::snprintf(buffer, sizeof buffer, "%.1f GiB/s", bytes_per_second / (1024.0 * 1024.0 * 1024.0));
    else if (bytes_per_second >= 1024.0 * 1024.0) std::snprintf(buffer, sizeof buffer, "%.0f MiB/s", bytes_per_second / (1024.0 * 1024.0));
    else std::snprintf(buffer, sizeof buffer, "%.0f KiB/s", bytes_per_second / 1024.0);
    return buffer;
}
// "1.8 GiB / 4.2 GiB  96 MiB/s  ETA 00:25"
std::string progress_text(unsigned long long done, unsigned long long total, double elapsed) {
    const double speed = elapsed > 0.0 ? static_cast<double>(done) / elapsed : 0.0;
    std::string text = human_bytes(done) + " / " + human_bytes(total) + "  " + rate_text(speed);
    if (speed > 0.0 && total > done) text += "  ETA " + clock_mmss(static_cast<double>(total - done) / speed);
    return text;
}
}  // namespace

Result copy_file_progress(const std::string& from, const std::string& to, std::string_view mode) {
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;
    std::error_code ec;
    const unsigned long long total = fs::file_size(from, ec);
    if (ec) return fail("cannot read " + from + ": " + ec.message());

    info("Copying " + from);
    info("      to " + to + "   (" + human_bytes(total) + ")");
    const int number = log_command_begin("copy " + from + " -> " + to, g_dry_run);
    const auto started = Clock::now();
    const auto elapsed = [&] { return std::chrono::duration<double>(Clock::now() - started).count(); };
    if (g_dry_run) {
        log_command_end(number, 0, 0.0);
        return {};
    }

    const auto failed = [&](const std::string& why) {
        log_command_end(number, 1, elapsed());
        fs::remove(to, ec);
        return fail(why);
    };
    if (fs::path(to).has_parent_path()) fs::create_directories(fs::path(to).parent_path(), ec);
    const int in = ::open(from.c_str(), O_RDONLY);
    if (in < 0) return failed("cannot open " + from);
    const int out = ::open(to.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) {
        ::close(in);
        return failed("cannot create " + to);
    }

    std::vector<char> buffer(1 << 20);
    unsigned long long done = 0;
    unsigned long long holes = 0;
    auto last_report = Clock::now() - std::chrono::seconds(1);
    bool write_failed = false;
    for (;;) {
        const ssize_t n = ::read(in, buffer.data(), buffer.size());
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        // An all-zero block becomes a hole: sparse files stay sparse.
        if (std::all_of(buffer.begin(), buffer.begin() + n, [](char c) { return c == 0; })) {
            ::lseek(out, n, SEEK_CUR);
            holes += static_cast<unsigned long long>(n);
        } else {
            for (ssize_t written = 0; written < n;) {
                const ssize_t w = ::write(out, buffer.data() + written, static_cast<std::size_t>(n - written));
                if (w < 0 && errno == EINTR) continue;
                if (w < 0) {
                    write_failed = true;
                    break;
                }
                written += w;
            }
            if (write_failed) break;
        }
        done += static_cast<unsigned long long>(n);
        if (Clock::now() - last_report >= std::chrono::milliseconds(250)) {
            last_report = Clock::now();
            status_progress(total ? static_cast<int>(done * 100 / total) : 100, progress_text(done, total, elapsed()));
        }
    }
    if (::ftruncate(out, static_cast<off_t>(done)) != 0 && !write_failed) write_failed = true;   // also makes a hole at the very end
    ::close(in);
    ::close(out);
    status_finish();
    if (write_failed) return failed("write error on " + to + " (disk full?)");

    if (!mode.empty()) fs::permissions(to, static_cast<fs::perms>(std::strtoul(std::string(mode).c_str(), nullptr, 8)), ec);
    status_progress(100, progress_text(done, total, elapsed()));
    log_command_end(number, 0, elapsed());
    info("Copied 1 file, " + human_bytes(done) + " in " + clock_mmss(elapsed()) + " (" + rate_text(elapsed() > 0 ? static_cast<double>(done) / elapsed() : 0.0) +
         (holes ? ", " + human_bytes(holes) + " kept sparse" : std::string()) + ")");
    return {};
}

Result copy_tree_progress(const std::string& from_dir, const std::string& to_dir, std::string_view what) {
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;

    // Count first, so the numbers can be shown before and the percentage has a total.
    unsigned long long files = 0, dirs = 0, bytes = 0;
    std::error_code ec;
    fs::recursive_directory_iterator it(from_dir, fs::directory_options::skip_permission_denied, ec);
    for (const fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
        std::error_code e;
        if (it->is_directory(e) && !it->is_symlink(e)) {
            ++dirs;
        } else {
            ++files;
            const auto size = it->is_regular_file(e) ? it->file_size(e) : 0;
            if (!e) bytes += size;
        }
    }
    info("Copying " + std::string(what) + ": " + std::to_string(files) + " files, " + std::to_string(dirs) + " folders, " + human_bytes(bytes));
    info("      " + from_dir + "  ->  " + to_dir);

    const auto started = Clock::now();
    RunOptions options;
    options.progress_total = bytes;
    if (!run({"cp", "-a", "--reflink=auto", from_dir + "/.", to_dir + "/"}, options).ok()) return fail("could not copy " + std::string(what));
    status_finish();
    const double seconds = std::chrono::duration<double>(Clock::now() - started).count();
    info("Copied " + std::to_string(files) + " files, " + std::to_string(dirs) + " folders, " + human_bytes(bytes) + " in " + clock_mmss(seconds) + " (" +
         rate_text(seconds > 0 ? static_cast<double>(bytes) / seconds : 0.0) + ")");
    return {};
}

bool set_password(const std::string& root_dir, const std::string& user, const std::string& password) {
    if (user.empty() || password.empty() || password.find('\n') != std::string::npos) return false;
    std::string path = (::access("/run", W_OK) == 0 ? "/run" : "/tmp") + std::string("/cccu-pw-XXXXXX");
    const int fd = ::mkstemp(path.data());   // created with mode 0600
    if (fd < 0) return false;
    const std::string line = user + ":" + password + "\n";
    const bool written = ::write(fd, line.data(), line.size()) == static_cast<ssize_t>(line.size());
    ::close(fd);

    bool ok = false;
    if (written) {
        RunOptions options;
        options.stdin_file = path;
        const std::vector<std::string> argv = (root_dir.empty() || root_dir == "/")
                                                  ? std::vector<std::string>{"chpasswd"}
                                                  : std::vector<std::string>{"chroot", root_dir, "chpasswd"};
        ok = run(argv, options).ok();
    }
    ::unlink(path.c_str());
    return ok;
}

bool have_command(std::string_view name) {
    const char* path_env = std::getenv("PATH");
    if (!path_env) return false;
    std::string_view paths = path_env;
    while (!paths.empty()) {
        const auto colon = paths.find(':');
        const std::string dir(paths.substr(0, colon));
        if (!dir.empty()) {
            const std::string candidate = dir + "/" + std::string(name);
            if (::access(candidate.c_str(), X_OK) == 0) return true;
        }
        if (colon == std::string_view::npos) break;
        paths.remove_prefix(colon + 1);
    }
    return false;
}

}  // namespace cccu
