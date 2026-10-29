#include "proc.hpp"

#include "util.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <iostream>

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
        for (;;) {
            // Wake up every 200 ms even when the command is silent, so the live status line
            // (spinner + elapsed time) keeps moving during long steps.
            pollfd watch {pipefd[0], POLLIN, 0};
            const int ready = ::poll(&watch, 1, 200);
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
