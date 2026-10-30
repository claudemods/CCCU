#include "containers.hpp"

#include "incus.hpp"
#include "proc.hpp"
#include "rootfs.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>

namespace cccu {
namespace fs = std::filesystem;

namespace {

const fs::path kActiveFile = "/etc/cccu/active";
const fs::path kSessionScript = "/usr/local/bin/cccu-session";

std::string active_value(const char* key) {
    const std::string prefix = std::string(key) + "=";
    for (const auto& line : split_lines(read_text(kActiveFile))) {
        if (line.rfind(prefix, 0) == 0) return trim(line.substr(prefix.size()));
    }
    return {};
}

RunOptions quiet_query() {
    RunOptions options;
    options.mutating = false;
    options.capture = true;
    options.quiet = true;
    return options;
}

std::string state_of(const std::string& name) {
    return query({"incus", "list", "^" + name + "$", "-c", "s", "--format", "csv"});
}

// The first normal account (uid 1000-59999) inside the container: its desktop is the one that runs.
std::string detect_user(const std::string& container) {
    return trim(run({"incus", "exec", container, "--", "sh", "-c",
                     "awk -F: '$3>=1000 && $3<60000 && $7!~/(nologin|false)$/ {print $1; exit}' /etc/passwd"},
                    quiet_query())
                    .out);
}

bool valid_user(const std::string& user) {
    return !user.empty() && std::all_of(user.begin(), user.end(), [](char c) {
        return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_' || c == '-';
    });
}

}  // namespace

std::string active_container() { return active_value("CCCU_CONTAINER"); }

Result list_containers(bool plain) {
    if (!is_root()) return fail("This must run as root: sudo cccu containers");
    const std::string active = active_container();
    std::vector<std::pair<std::string, std::string>> rows;
    for (const auto& line : split_lines(query({"incus", "list", "--format", "csv", "-c", "ns"}))) {
        const auto comma = line.find(',');
        if (comma == std::string::npos) continue;
        rows.emplace_back(trim(line.substr(0, comma)), trim(line.substr(comma + 1)));
    }
    if (plain) {
        for (const auto& [name, state] : rows) std::cout << name << '\t' << state << '\t' << (name == active ? "active" : "") << '\n';
        return {};
    }
    if (rows.empty()) {
        info("There are no containers yet.");
        return {};
    }
    for (const auto& [name, state] : rows) {
        info(name + "   " + state + (name == active ? "   <- shown by the host session" : ""));
    }
    if (active.empty()) info("No container was switched to yet: the host session shows the one it was installed for.");
    return {};
}

Result set_container_running(const std::string& name, bool running) {
    if (!is_root()) return fail("This must run as root.");
    if (!valid_instance_name(name) || (!dry_run() && !incus::exists(name))) return fail("There is no container named '" + name + "'.");
    const std::string state = state_of(name);
    if (running) {
        if (state == "RUNNING") return {};
        return incus::start(name);
    }
    if (state != "RUNNING") return {};
    if (!run({"incus", "stop", name}).ok()) return fail("could not stop " + name);
    return {};
}

Result duplicate_container(Options o) {
    if (!is_root()) return fail("This must run as root: sudo cccu duplicate <source> <new-name>");
    if (o.name.empty() || o.target.empty()) return fail("Usage: cccu duplicate <container> <new-name>");
    if (!valid_instance_name(o.target)) return fail("Invalid name '" + o.target + "' (letters, digits and '-', must start with a letter).");
    if (!dry_run()) {
        if (!incus::daemon_ready()) return fail("Incus is not answering (systemctl status incus).");
        if (!incus::exists(o.name)) return fail("There is no container named '" + o.name + "'.");
        if (incus::exists(o.target)) return fail("A container named '" + o.target + "' exists already.");
    }
    // Immutable containers keep /etc, /var, /home ... on their own volumes; a plain copy would share them.
    if (query({"incus", "config", "device", "list", o.name}).find("state-") != std::string::npos) {
        return fail("'" + o.name + "' is immutable (its /etc, /var, /home ... are separate volumes); duplicating those is not supported yet.");
    }

    set_total_steps(o.switch_now ? 3 : 2);
    step("Copying '" + o.name + "' to '" + o.target + "' (a full, independent copy)");
    if (!run({"incus", "copy", o.name, o.target}).ok()) return fail("incus copy failed (is there room in the storage pool?)");
    // Two desktops must not both come up at boot: only the active one autostarts.
    incus::set_config(o.target, "boot.autostart", "false");

    step("Checking the copy");
    ok("'" + o.target + "' is ready. It is an exact copy as of now; changes in one do not affect the other.");
    if (!o.switch_now) {
        info("Switch to it with:  cccu switch " + o.target);
        return {};
    }
    step("Switching to '" + o.target + "'");
    o.name = o.target;
    return switch_container(o);
}

Result switch_container(Options o) {
    if (!is_root()) return fail("This must run as root: sudo cccu switch <container>");
    if (o.name.empty()) return fail("Usage: cccu switch <container>");
    if (!valid_instance_name(o.name)) return fail("Invalid container name '" + o.name + "'.");
    if (!dry_run()) {
        if (!incus::daemon_ready()) return fail("Incus is not answering (systemctl status incus).");
        if (!incus::exists(o.name)) return fail("There is no container named '" + o.name + "'. See: cccu containers");
    }
    std::string previous = active_container();
    if (previous.empty()) {   // never switched: the session script still names the container it was installed for
        for (const auto& line : split_lines(read_text(kSessionScript))) {
            if (line.rfind("C=", 0) == 0 && line.find("__") == std::string::npos) {
                previous = trim(line.substr(2, line.find(' ') == std::string::npos ? std::string::npos : line.find(' ') - 2));
                break;
            }
        }
    }

    // The account whose desktop runs inside the new container.
    if (state_of(o.name) != "RUNNING") {
        if (auto r = incus::start(o.name); !r) return r;
        incus::wait_ready(o.name);
    }
    std::string user = o.container_user;
    if (user.empty() && previous == o.name) user = active_value("CCCU_USER");
    if (user.empty()) user = dry_run() ? std::string("user") : detect_user(o.name);
    if (!valid_user(user)) return fail("Could not find a user account inside '" + o.name + "'. Give one with --container-user <name>.");
    o.container_user = user;

    std::error_code ec;
    if (!dry_run()) {
        fs::create_directories(kActiveFile.parent_path(), ec);
        if (auto r = write_text(kActiveFile, "CCCU_CONTAINER=" + o.name + "\nCCCU_USER=" + user + "\n"); !r) return r;
        // A host installed by an older CCCU does not read the file yet: rewrite its session script.
        if (fs::exists(kSessionScript, ec)) rootfs::refresh_session(o);
    }
    incus::set_config(o.name, "boot.autostart", "true");

    if (!previous.empty() && previous != o.name && !o.keep_old && (dry_run() || incus::exists(previous))) {
        info("Stopping the previous container '" + previous + "' (two desktops cannot share the screen and GPU).");
        incus::set_config(previous, "boot.autostart", "false");
        if (state_of(previous) == "RUNNING" && !run({"incus", "stop", previous}).ok()) warn("could not stop '" + previous + "'");
    }
    ok("Active container: '" + o.name + "' (user " + user + ").");
    if (fs::exists(kSessionScript, ec)) {
        info("Log out and choose \"Container Plasma\" at the login screen to see it.");
    } else {
        info("Native mode: stop the host's display manager (systemctl stop display-manager) so the container's login takes the screen.");
    }
    return {};
}

}  // namespace cccu
