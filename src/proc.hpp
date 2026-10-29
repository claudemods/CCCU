#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace claudeincus {

struct RunOptions {
    bool mutating = true;      // false = read-only query, still runs under --dry-run
    bool capture = false;      // capture stdout instead of showing it
    bool quiet = false;        // do not echo the command line on screen (it is still logged)
    bool interactive = false;  // inherit the terminal untouched (passwords, progress bars); output not logged
    std::filesystem::path stdin_file;
};

struct RunResult {
    int code = -1;
    std::string out;
    bool ok() const { return code == 0; }
};

void set_dry_run(bool enabled);
bool dry_run();

// Runs argv directly (no shell, so no quoting problems). Output (stdout+stderr) is shown on the
// terminal and written to the log unless capture/interactive is set.
RunResult run(const std::vector<std::string>& argv, const RunOptions& options = {});

// Read-only helper: runs a query and returns its trimmed stdout ("" on failure).
std::string query(const std::vector<std::string>& argv);

bool have_command(std::string_view name);
bool is_root();

}  // namespace claudeincus
