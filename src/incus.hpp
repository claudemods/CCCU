#pragma once
#include "config.hpp"
#include "util.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace claudeincus::incus {

// True when `incus list` works; tries to start/enable the service once when it does not.
bool daemon_ready();

std::vector<std::string> storage_pools();
std::string storage_driver(const std::string& pool);
bool exists(const std::string& name);

bool image_exists(const std::string& alias);
Result import_image(const std::filesystem::path& tarball, const std::string& alias);
Result delete_image(const std::string& alias);

// Creates a privileged, nesting-enabled system container (not started).
Result init(const std::string& name, const std::string& image, const std::vector<std::string>& profiles,
            const std::string& pool);
Result start(const std::string& name);
Result set_config(const std::string& name, const std::string& key, const std::string& value);

// Makes sure `file` (/etc/subuid or /etc/subgid) gives root the id range Incus expects.
// Returns true when the file was changed.
bool ensure_subid(const std::filesystem::path& file);
Result remove(const std::string& name);

Result ensure_profile(const std::string& name, const std::string& yaml);

// Waits until systemd inside the container finished booting (running or degraded).
void wait_ready(const std::string& name);

// Host path of the instance rootfs for dir/btrfs pools.
std::filesystem::path rootfs_path(const Options& options);

// A tiny image (metadata.yaml + empty rootfs/) used to create an instance we then fill by hand.
Result ensure_empty_image(const std::string& alias, const std::filesystem::path& workdir);

// metadata.yaml for an image built from a staged rootfs.
std::string image_metadata(const std::string& description);

}  // namespace claudeincus::incus
