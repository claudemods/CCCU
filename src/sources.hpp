#pragma once
#include "config.hpp"
#include "util.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace claudeincus::sources {

// rsync exclude patterns (relative to /) for cloning the live system.
std::vector<std::string> clone_excludes(const Options& options);

// Unpacks a squashfs, erofs or tar(.zst/.xz/.gz) image into `root` (an existing directory).
Result extract_image(const std::string& image, const std::filesystem::path& root);

// Host tools needed to unpack `image`.
std::vector<std::string> image_tools(const std::string& image);

// Fills `root` from options.image, or from the running system when no image is given.
Result populate(const Options& options, const std::filesystem::path& root);

// rsync-clones the system we are running on into `root`.
Result clone_running(const Options& options, const std::filesystem::path& root);

// Host tools the chosen source needs.
std::vector<std::string> required_tools(const Options& options);

// Writes the running system to a tar image (.tar, .tar.zst, .tar.xz, .tar.gz) for another machine.
Result export_host(const Options& options, const std::filesystem::path& out);

}  // namespace claudeincus::sources
