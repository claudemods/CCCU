#include "incus.hpp"

#include "proc.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace claudeincus::incus {
namespace fs = std::filesystem;

namespace {
std::vector<std::string> csv_column(const std::vector<std::string>& argv) {
    std::vector<std::string> values;
    for (const auto& line : split_lines(query(argv))) {
        const std::string value = trim(line);
        if (!value.empty()) values.push_back(value);
    }
    return values;
}
}  // namespace

bool daemon_ready() {
    RunOptions probe;
    probe.mutating = false;
    probe.capture = true;
    probe.quiet = true;
    if (run({"incus", "list", "--format", "csv"}, probe).ok()) return true;
    warn("Incus is not answering; trying to start the service...");
    run({"systemctl", "enable", "--now", "incus.socket", "incus.service"});
    return run({"incus", "list", "--format", "csv"}, probe).ok();
}

std::vector<std::string> storage_pools() {
    return csv_column({"incus", "storage", "list", "--format", "csv", "-c", "n"});
}

std::string storage_driver(const std::string& pool) {
    for (const auto& line : split_lines(query({"incus", "storage", "show", pool}))) {
        if (line.rfind("driver:", 0) == 0) return trim(line.substr(7));
    }
    return {};
}

bool exists(const std::string& name) {
    const auto all = csv_column({"incus", "list", "--format", "csv", "-c", "n"});
    return std::find(all.begin(), all.end(), name) != all.end();
}

bool image_exists(const std::string& alias) {
    return query({"incus", "image", "list", alias, "--format", "csv", "-c", "l"}).find(alias) != std::string::npos;
}

Result import_image(const fs::path& tarball, const std::string& alias) {
    if (!run({"incus", "image", "import", tarball.string(), "--alias", alias}).ok()) {
        return fail("incus image import failed for " + tarball.string());
    }
    return {};
}

Result delete_image(const std::string& alias) {
    if (!run({"incus", "image", "delete", alias}).ok()) return fail("could not delete image " + alias);
    return {};
}

Result init(const std::string& name, const std::string& image, const std::vector<std::string>& profiles,
            const std::string& pool) {
    std::vector<std::string> argv = {"incus", "init", image, name, "--storage", pool,
                                     "-c", "security.privileged=true",
                                     "-c", "security.nesting=true",
                                     "--no-profiles", "-p", "default"};
    for (const auto& profile : profiles) {
        argv.push_back("-p");
        argv.push_back(profile);
    }
    if (!run(argv).ok()) return fail("incus init failed for " + name);
    return {};
}

Result start(const std::string& name) {
    if (!run({"incus", "start", name}).ok()) return fail("could not start " + name);
    return {};
}

Result set_config(const std::string& name, const std::string& key, const std::string& value) {
    if (!run({"incus", "config", "set", name, key, value}).ok()) return fail("could not set " + key + " on " + name);
    return {};
}

bool ensure_subid(const fs::path& file) {
    const std::string current = read_text(file);
    for (const auto& line : split_lines(current)) {
        if (line.rfind("root:", 0) == 0) return false;
    }
    info("Adding root subordinate id range to " + file.string());
    if (dry_run()) return true;
    const std::string updated = current + ((current.empty() || current.back() == '\n') ? "" : "\n") +
                                "root:1000000:1000000000\n";
    if (auto w = write_text(file, updated); !w) {
        warn(w.error());
        return false;
    }
    return true;
}

Result remove(const std::string& name) {
    if (!run({"incus", "delete", name, "--force"}).ok()) return fail("could not delete " + name);
    return {};
}

Result ensure_profile(const std::string& name, const std::string& yaml) {
    const auto all = csv_column({"incus", "profile", "list", "--format", "csv", "-c", "n"});
    const bool known = std::find(all.begin(), all.end(), name) != all.end();
    if (!known && !run({"incus", "profile", "create", name}).ok()) {
        return fail("could not create profile " + name);
    }
    if (dry_run()) {
        info("(dry run) profile " + name + " would be set to:\n" + yaml);
        return {};
    }
    const fs::path tmp = fs::temp_directory_path() / ("claudeincus-profile-" + name + ".yaml");
    if (auto written = write_text(tmp, yaml); !written) return written;
    RunOptions options;
    options.stdin_file = tmp;
    const bool applied = run({"incus", "profile", "edit", name}, options).ok();
    std::error_code ec;
    fs::remove(tmp, ec);
    if (!applied) return fail("could not apply profile " + name);
    return {};
}

void wait_ready(const std::string& name) {
    if (dry_run()) return;
    // `incus exec` fails until systemd inside is up, so poll. "degraded" still counts as booted
    // (some units legitimately fail inside a container).
    RunOptions probe;
    probe.mutating = false;
    probe.quiet = true;
    probe.capture = true;
    info("Waiting for '" + name + "' to finish booting...");
    for (int attempt = 0; attempt < 90; ++attempt) {
        const std::string state =
            trim(run({"incus", "exec", name, "--", "systemctl", "is-system-running"}, probe).out);
        if (state == "running" || state == "degraded") {
            info("Container boot finished (" + state + ").");
            return;
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    warn("The container did not finish booting within 3 minutes; continuing anyway.");
}

fs::path rootfs_path(const Options& options) {
    return fs::path(options.incus_dir) / "storage-pools" / options.pool / "containers" / options.name / "rootfs";
}

std::string image_metadata(const std::string& description) {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    return "architecture: x86_64\n"
           "creation_date: " + std::to_string(now) + "\n"
           "properties:\n"
           "  architecture: x86_64\n"
           "  description: " + description + "\n"
           "  os: Archlinux\n"
           "  release: current\n";
}

Result ensure_empty_image(const std::string& alias, const fs::path& workdir) {
    if (image_exists(alias)) return {};
    if (dry_run()) {
        info("(dry run) would import empty bootstrap image " + alias);
        return {};
    }
    std::error_code ec;
    fs::create_directories(workdir / "rootfs", ec);
    if (ec) return fail("cannot create " + (workdir / "rootfs").string());
    if (auto w = write_text(workdir / "metadata.yaml", image_metadata("claudeincus empty bootstrap")); !w) return w;
    const fs::path tarball = workdir / "empty.tar";
    if (!run({"tar", "-C", workdir.string(), "-cf", tarball.string(), "metadata.yaml", "rootfs"}).ok()) {
        return fail("could not build bootstrap image");
    }
    return import_image(tarball, alias);
}

}  // namespace claudeincus::incus
