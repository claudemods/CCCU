#include "sources.hpp"

#include "proc.hpp"

#include <cstdlib>
#include <fstream>

namespace claudeincus::sources {
namespace fs = std::filesystem;

namespace {

bool is_squashfs(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    char magic[4] = {};
    in.read(magic, 4);
    return in && std::string(magic, 4) == "hsqs";
}

// EROFS magic 0xE0F5E1E2 (little endian) at offset 1024.
bool is_erofs(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    unsigned char magic[4] = {};
    in.seekg(1024);
    in.read(reinterpret_cast<char*>(magic), 4);
    return in && magic[0] == 0xE2 && magic[1] == 0xE1 && magic[2] == 0xF5 && magic[3] == 0xE0;
}

bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

Result clone_host(const Options& options, const fs::path& root);

}  // namespace

std::vector<std::string> clone_excludes(const Options& options) {
    std::vector<std::string> excludes = {
        // Pseudo / runtime filesystems: recreated empty by the container.
        "/dev/*", "/proc/*", "/sys/*", "/run/*", "/tmp/*", "/var/tmp/*",
        // Mounts that belong to the host.
        "/mnt/*", "/media/*", "/lost+found", "/.snapshots", "/swapfile", "/swap",
        // Things that must not exist twice or make no sense in a container.
        "/var/lib/incus", "/var/log/incus", "/var/lib/lxd", "/var/lib/docker", "/var/lib/containers",
        "/var/lib/machines/*", "/var/lib/libvirt/images",
        "/etc/fstab", "/etc/crypttab", "/etc/mtab", "/etc/mdadm.conf",
        "/etc/udev/rules.d/70-persistent-cd.rules", "/etc/udev/rules.d/70-persistent-net.rules",
        "/etc/NetworkManager/system-connections/*",
        "/etc/pacman.d/gnupg/S.*", "/var/lib/pacman/db.lck",
        "/var/lib/dbus/machine-id",
    };
    if (!options.keep_kernel) excludes.push_back("/boot/*");
    if (!options.keep_caches) {
        excludes.push_back("/var/cache/pacman/pkg/*");
        excludes.push_back("/home/*/.cache/*");
        excludes.push_back("/root/.cache/*");
    }
    for (const auto& extra : options.extra_excludes) excludes.push_back(extra);
    return excludes;
}

std::string describe_image(const std::string& image) {
    if (is_squashfs(image)) return "squashfs";
    if (is_erofs(image)) return "erofs";
    if (ends_with(image, ".zst") || ends_with(image, ".zstd")) return "zstd-compressed tar";
    if (ends_with(image, ".xz")) return "xz-compressed tar";
    if (ends_with(image, ".gz") || ends_with(image, ".tgz")) return "gzip-compressed tar";
    return "tar";
}

unsigned long long estimate_unpacked_bytes(const std::string& image) {
    std::error_code ec;
    const double packed = static_cast<double>(fs::file_size(image, ec));
    if (ec) return 0;
    double factor = 1.0;
    if (is_squashfs(image) || is_erofs(image)) factor = 2.0;
    else if (ends_with(image, ".zst") || ends_with(image, ".zstd")) factor = 3.0;
    else if (ends_with(image, ".xz")) factor = 3.5;
    else if (ends_with(image, ".gz") || ends_with(image, ".tgz")) factor = 2.5;
    return static_cast<unsigned long long>(packed * factor);
}

std::vector<std::string> image_users(const std::string& image) {
    std::string text;
    if (image.empty()) {
        text = read_text("/etc/passwd");
    } else {
        std::vector<std::string> argv;
        if (is_squashfs(image)) argv = {"unsquashfs", "-cat", image, "etc/passwd"};
        else if (is_erofs(image)) return {};
        else argv = {"tar", "-xOf", image, "./etc/passwd"};   // GNU tar detects zstd/xz/gzip by itself
        RunOptions probe;
        probe.mutating = false;
        probe.capture = true;
        probe.quiet = true;
        text = run(argv, probe).out;
    }
    std::vector<std::string> users;
    for (const auto& line : split_lines(text)) {
        std::vector<std::string> f;
        std::string field;
        for (char c : line) {
            if (c == ':') { f.push_back(field); field.clear(); }
            else field.push_back(c);
        }
        f.push_back(field);
        if (f.size() < 7) continue;
        const long uid = std::strtol(f[2].c_str(), nullptr, 10);
        if (uid < 1000 || uid >= 60000) continue;
        if (ends_with(f[6], "nologin") || ends_with(f[6], "/false")) continue;
        users.push_back(f[0]);
    }
    return users;
}

std::vector<std::string> image_tools(const std::string& image) {
    if (is_squashfs(image)) return {"unsquashfs"};
    if (is_erofs(image)) return {"fsck.erofs"};
    std::vector<std::string> tools = {"tar"};
    if (ends_with(image, ".zst") || ends_with(image, ".zstd")) tools.push_back("zstd");
    if (ends_with(image, ".xz")) tools.push_back("xz");
    return tools;
}

std::vector<std::string> required_tools(const Options& options) {
    if (options.image.empty()) return {"rsync"};
    return image_tools(options.image);
}

Result extract_image(const std::string& image, const fs::path& root) {
    if (is_squashfs(image)) {
        // -f lets unsquashfs write into the existing directory.
        RunOptions terminal;
        terminal.interactive = true;  // keep unsquashfs's progress bar on screen
        if (!run({"unsquashfs", "-f", "-d", root.string(), image}, terminal).ok()) return fail("unsquashfs failed for " + image);
        return {};
    }
    if (is_erofs(image)) {
        if (!run({"fsck.erofs", "--extract=" + root.string(), image}).ok()) return fail("fsck.erofs failed for " + image);
        return {};
    }
    if (!run({"tar", "--numeric-owner", "--xattrs", "--xattrs-include=*", "--acls", "-xpf", image,
              "-C", root.string()})
             .ok()) {
        return fail("could not extract " + image);
    }
    return {};
}

Result export_host(const Options& options, const fs::path& out) {
    const std::string name = out.string();
    std::vector<std::string> argv = {"tar", "--numeric-owner", "--xattrs", "--xattrs-include=*", "--acls"};
    if (ends_with(name, ".zst")) argv.push_back("--zstd");
    else if (ends_with(name, ".xz")) argv.push_back("-J");
    else if (ends_with(name, ".gz")) argv.push_back("-z");
    for (const auto& pattern : clone_excludes(options)) argv.push_back("--exclude=." + pattern);
    std::error_code ec;
    argv.push_back("--exclude=." + fs::absolute(out, ec).string());
    argv.insert(argv.end(), {"-cpf", name, "-C", "/", "."});

    info("Writing " + name + " (this takes a while)");
    const RunResult result = run(argv);
    // 1 = some files changed while being read: normal for a live system.
    if (!result.ok() && result.code != 1) return fail("tar failed (exit " + std::to_string(result.code) + ")");
    return {};
}

Result clone_running(const Options& options, const fs::path& root) { return clone_host(options, root); }

Result populate(const Options& options, const fs::path& root) {
    if (options.image.empty()) return clone_host(options, root);
    return extract_image(options.image, root);
}

namespace {

Result clone_host(const Options& options, const fs::path& root) {
    std::vector<std::string> argv = {"rsync", "-aHAXS", "--numeric-ids", "--info=progress2"};
    for (const auto& pattern : clone_excludes(options)) argv.push_back("--exclude=" + pattern);
    argv.push_back("/");
    argv.push_back(root.string() + "/");

    info("Copying / -> " + root.string());
    const RunResult result = run(argv);
    // 24 = files vanished while copying: normal when cloning a live system.
    if (!result.ok() && result.code != 24) return fail("rsync failed (exit " + std::to_string(result.code) + ")");
    return {};
}

}  // namespace

}  // namespace claudeincus::sources
