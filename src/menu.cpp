#include "menu.hpp"

#include "bootstrap.hpp"
#include "proc.hpp"
#include "sources.hpp"
#include "util.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <vector>

namespace cccu::menu {
namespace {
namespace fs = std::filesystem;

constexpr std::size_t kInner = 76;   // width inside the box borders

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string repeat(const char* glyph, std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) out += glyph;
    return out;
}

// ---- box drawing ------------------------------------------------------------------------------

void box_top(std::string_view title) {
    const std::string head = "─ " + std::string(title) + " ";
    std::cout << paint(color::cyan, "┌" + head + repeat("─", kInner - visible_width(head)) + "┐") << '\n';
}
void box_rule() { std::cout << paint(color::cyan, "├" + repeat("─", kInner) + "┤") << '\n'; }
void box_bottom() { std::cout << paint(color::cyan, "└" + repeat("─", kInner) + "┘") << '\n'; }
void box_line(const std::string& content) {
    std::cout << paint(color::cyan, "│") << fit(content, kInner) << paint(color::cyan, "│") << '\n';
}

// The status box of a row: green SET or red NOT SET, 11 columns wide.
std::string badge(bool set) {
    return paint(set ? color::badge_ok : color::badge_bad, set ? "    SET    " : "  NOT SET  ");
}

// ---- the questions ----------------------------------------------------------------------------

// Compression of the container's btrfs storage (btrfs mount option compress=<choice>).
std::string ask_compression(const std::string& current) {
    info("");
    info("Compression for the container's btrfs storage (saves space, costs some CPU):");
    info("  1) zstd:22   strongest");
    info("  2) zstd:15");
    info("  3) zstd:9");
    info("  4) zstd:3    fast and still good");
    info("  5) zlib:9");
    info("  6) lzo       fastest");
    info("  7) none");
    static const char* const choices[] = {"zstd:22", "zstd:15", "zstd:9", "zstd:3", "zlib:9", "lzo", "none"};
    for (int attempt = 0; attempt < 5; ++attempt) {
        const std::string c = ask("Choose (1-7)");
        if (c.empty()) break;
        if (c.size() == 1 && c[0] >= '1' && c[0] <= '7') return choices[c[0] - '1'];
        error("Type a number from 1 to 7.");
    }
    return current.empty() ? "none" : current;
}

// The container size is always asked; there is no default. The hint comes from the image itself.
std::string ask_container_size(const Options& o) {
    constexpr double kGiB = 1024.0 * 1024.0 * 1024.0;
    std::error_code ec;
    unsigned long long unpacked = 0;
    unsigned long long hint = 8;

    info("");
    info("Container size = the root disk of the container: your whole desktop, its apps and updates live in it.");
    if (!o.image.empty() && fs::is_regular_file(o.image, ec)) {
        const unsigned long long packed = fs::file_size(o.image, ec);
        unpacked = sources::estimate_unpacked_bytes(o.image);
        hint = std::max<unsigned long long>(8, static_cast<unsigned long long>(std::ceil(static_cast<double>(unpacked) * 1.5 / kGiB)));
        info("Your image is " + human_bytes(packed) + " (" + sources::describe_image(o.image) + "); unpacked it needs roughly " +
             human_bytes(unpacked) + ".");
        info("Leave room for updates and new apps: about " + std::to_string(hint) + " GiB or more is a sensible size.");
    } else if (o.clone_host) {
        info("Cloning this system copies what is installed now: leave room for it plus updates and new apps (see: df -h /).");
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        const std::string size = normalize_size(ask("Container size in GiB", o.container_size));
        if (size.empty()) {
            error("Type a number of GiB, for example " + std::to_string(hint) + ".");
            continue;
        }
        if (unpacked > 0 && std::stod(size) * kGiB < static_cast<double>(unpacked)) {
            warn("That is smaller than the unpacked image (" + human_bytes(unpacked) + "): the system will not fit.");
            if (!confirm("Use " + size + " anyway?", false)) continue;
        }
        return size;
    }
    return o.container_size;
}

// The user INSIDE the cloned system: its desktop is what runs, nothing is created there.
void edit_container_user(Options& o) {
    const std::vector<std::string> users = sources::image_users(o.image);   // empty image = this running system
    info("");
    info("The container is a clone, so its users already exist (nothing is created there).");
    std::string fallback = !o.container_user.empty() ? o.container_user : o.user;
    if (!users.empty()) {
        info("Users found in the cloned system:");
        for (std::size_t i = 0; i < users.size(); ++i) info("  " + std::to_string(i + 1) + ") " + users[i]);
        if (fallback.empty()) fallback = users.front();
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::string answer = ask(users.empty() ? "Username in the cloned system" : "Username in the cloned system (number or name)", fallback);
        if (answer.empty()) {
            error("A username is required.");
            continue;
        }
        if (std::all_of(answer.begin(), answer.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
            const std::size_t n = static_cast<std::size_t>(std::stoul(answer));
            if (n >= 1 && n <= users.size()) answer = users[n - 1];
        }
        if (!users.empty() && std::find(users.begin(), users.end(), answer) == users.end()) {
            warn("'" + answer + "' is not a user of the cloned system: the container could not start the desktop as that user.");
            if (!confirm("Use it anyway?", false)) continue;
        }
        o.container_user = answer;
        return;
    }
}

// The account on the tty host: it can have a different name than the one in the clone.
void edit_host_user(Options& o) {
    info("");
    info("Account on the tty host: created if missing, gets Incus access and is logged in automatically at boot.");
    for (int attempt = 0; attempt < 5; ++attempt) {
        const std::string answer = ask("Username on the tty host", !o.user.empty() ? o.user : o.container_user);
        if (answer.empty()) {
            error("A username is required: the tty host logs this account in automatically.");
        } else if (!valid_username(answer)) {
            error("Lowercase letters, digits, - and _ only.");
        } else {
            o.user = answer;
            return;
        }
    }
}

void edit_name(Options& o) {
    const std::string answer = ask("Container name", o.name);
    if (valid_instance_name(answer)) o.name = answer;
    else error("Invalid container name (letters, digits and '-', must start with a letter).");
}

void edit_immutable(Options& o) {
    info("");
    info("Immutable container: its system (/usr, /bin, /lib ...) is mounted read-only, so nothing can change it by accident.");
    info("/etc, /var, /home, /root, /opt, /srv and /usr/local stay writable (their own volumes).");
    info("To update the system later:  incus config device set " + o.name + " root readonly=false   (then restart it).");
    o.immutable = confirm("Make the container immutable?", o.immutable);
}

// ---- rows -------------------------------------------------------------------------------------

struct Row {
    std::string label;
    std::string value;                 // empty = not set
    bool required = false;
    std::function<void()> edit;
};

std::string or_none(const std::string& text) { return text.empty() ? "none" : text; }

std::string host_source_text(const Options& o) {
    if (o.clone_system) return "clone of this running system";
    if (!o.host_iso.empty()) return "ISO: " + o.host_iso;
    if (const bootstrap::Preset* preset = bootstrap::find(o.host_bootstrap)) return "base list: " + preset->title;
    return o.host_image.empty() ? "live ISO image (default)" : o.host_image;
}

// Where the tty host system comes from. The base package lists are built in: the user picks one and sees
// what it contains, nobody types a package list.
void edit_host_source(Options& o, std::string& notice) {
    info("");
    info("Where does the tty host system come from?");
    info("  1) The live ISO you booted (default)");
    info("  2) A clone of the running system");
    info("  3) A system image file (squashfs / erofs / tar)");
    info("  4) An ISO file (its root file system is used)");
    info("  5) A fresh base system from a built-in package list (downloaded and installed)");
    const std::string choice = ask("Choose (1-5)");
    if (choice.empty()) return;

    std::error_code ec;
    const auto reset = [&o] {
        o.clone_system = false;
        o.host_image.clear();
        o.host_iso.clear();
        o.host_bootstrap.clear();
    };
    if (choice == "1") {
        reset();
    } else if (choice == "2") {
        reset();
        o.clone_system = true;
    } else if (choice == "3") {
        const std::string path = ask("Path to the system image");
        if (fs::is_regular_file(path, ec)) {
            reset();
            o.host_image = path;
        } else {
            notice = "File not found: " + path;
        }
    } else if (choice == "4") {
        const std::string path = ask("Path to the ISO file");
        if (fs::is_regular_file(path, ec)) {
            reset();
            o.host_iso = path;
        } else {
            notice = "File not found: " + path;
        }
    } else if (choice == "5") {
        const auto& lists = bootstrap::presets();
        info("");
        info("Base package lists:");
        for (std::size_t i = 0; i < lists.size(); ++i) {
            std::string packages;
            for (const auto& p : lists[i].packages) packages += (packages.empty() ? "" : " ") + p;
            info("  " + std::to_string(i + 1) + ") " + lists[i].title);
            info("       " + packages);
        }
        const std::string pick = ask("Choose (1-" + std::to_string(lists.size()) + ")");
        if (pick.size() == 1 && pick[0] >= '1' && pick[0] < static_cast<char>('1' + lists.size())) {
            reset();
            o.host_bootstrap = lists[static_cast<std::size_t>(pick[0] - '1')].id;
        } else if (!pick.empty()) {
            notice = "Choose a number from 1 to " + std::to_string(lists.size()) + ".";
        }
    } else {
        notice = "Type a number from 1 to 5.";
    }
}

std::vector<Row> rows_for(Mode mode, Options& o, Flags& flags, std::string& notice) {
    const auto effective_container_user = [&o] { return o.container_user.empty() ? o.user : o.container_user; };
    std::vector<Row> rows;

    if (mode == Mode::Export) {
        rows.push_back({"Output file", o.export_path, true, [&o] { o.export_path = ask("Output file (.tar .tar.zst .tar.xz .tar.gz)", o.export_path); }});
        return rows;
    }

    if (mode == Mode::Install) {
        rows.push_back({"Target drive (WIPED)", o.drive, true, [&o, &notice] {
            RunOptions listing;
            listing.mutating = false;
            run({"lsblk", "-d", "-o", "NAME,SIZE,MODEL,TRAN"}, listing);
            const std::string answer = ask("Drive to WIPE and install onto (e.g. /dev/sda)", o.drive);
            if (answer.rfind("/dev/", 0) == 0) o.drive = answer;
            else notice = "A drive is a device path such as /dev/sda or /dev/nvme0n1.";
        }});
        rows.push_back({"Desktop image", o.image, true, [&o, &notice] {
            const std::string answer = ask("Path to the desktop image made with --export", o.image);
            std::error_code ec;
            if (fs::is_regular_file(answer, ec)) o.image = answer;
            else notice = "File not found: " + answer;
        }});
        rows.push_back({"tty host system", host_source_text(o), false, [&o, &notice] { edit_host_source(o, notice); }});
        rows.push_back({"Hostname", o.hostname, true, [&o, &notice] {
            const std::string answer = ask("Hostname for the new system", o.hostname);
            if (valid_hostname(answer)) o.hostname = answer;
            else notice = "A hostname has letters, digits and - only.";
        }});
    } else {
        rows.push_back({"System to clone", o.clone_host ? std::string("clone of THIS running system") : o.image, true, [&o, &notice] {
            const std::string answer = ask("Path to the system image (squashfs/tar made with --export), or 'host' to clone THIS running system");
            std::error_code ec;
            if (answer == "host") {
                o.clone_host = true;
                o.image.clear();
            } else if (fs::is_regular_file(answer, ec)) {
                o.clone_host = false;
                o.image = answer;
            } else if (!answer.empty()) {
                notice = "File not found: " + answer;
            }
        }});
    }

    rows.push_back({"User on the tty host", o.user, true, [&o] { edit_host_user(o); }});
    rows.push_back({"User in the clone", effective_container_user(), true, [&o] { edit_container_user(o); }});
    if (mode == Mode::Install) {
        rows.push_back({"System filesystem", o.fs, true, [&o, &flags, &notice] {
            const std::string answer = lower(ask("System filesystem (ext4 or btrfs)", o.fs));
            if (answer == "ext4" || answer == "btrfs") {
                o.fs = answer;
                flags.fs_given = true;
            } else {
                notice = "Choose ext4 or btrfs.";
            }
        }});
    }
    rows.push_back({"Container name", o.name, true, [&o] { edit_name(o); }});
    rows.push_back({"Container size", o.container_size.empty() ? std::string() : o.container_size, true, [&o] { o.container_size = ask_container_size(o); }});
    rows.push_back({"Compression", or_none(o.compression), true, [&o, &flags] {
        o.compression = ask_compression(o.compression);
        if (o.compression == "none") o.compression.clear();
        flags.compress_given = true;
    }});
    rows.push_back({"Immutable container", o.immutable ? "yes (read-only system)" : "no", true, [&o] { edit_immutable(o); }});
    return rows;
}

const char* mode_title(Mode mode) {
    switch (mode) {
        case Mode::Install: return "INSTALL TO A DRIVE  -  wipes the drive: EFI boot + tty host + desktop container";
        case Mode::Running: return "SET UP ON THIS SYSTEM  -  installs what the host needs, then the container";
        case Mode::Export: return "EXPORT THIS MACHINE  -  write it as a desktop image";
    }
    return "";
}

void render(Mode mode, const std::vector<Row>& rows, const std::string& notice) {
    clear_screen();
    print_banner();
    box_top(mode_title(mode));
    box_line(" " + paint(color::grey, fit(" #", 3) + " " + fit("SETTING", 22) + " " + fit("VALUE", 33) + " " + fit("STATUS", 11)));
    box_rule();
    std::size_t missing = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& row = rows[i];
        const bool set = !row.value.empty();
        if (!set && row.required) ++missing;
        const std::string value = set ? row.value : "-";
        box_line(" " + paint(color::cyan, fit(std::to_string(i + 1), 3)) + " " + paint(color::white, fit(row.label, 22)) + " " +
                 fit(value, 33) + " " + badge(set) + " ");
    }
    box_rule();
    if (missing == 0) {
        box_line(" " + paint(color::badge_ok, "  READY  ") + "  everything is set: press S to start");
    } else {
        box_line(" " + paint(color::badge_bad, " MISSING ") + "  " + std::to_string(missing) + " required setting" + (missing == 1 ? "" : "s") +
                 " not set yet");
    }
    box_line(" " + paint(color::grey, "Type a number to change a setting   S = start   Q = quit"));
    box_bottom();
    if (!notice.empty()) std::cout << '\n' << paint(color::red, "  ! " + notice) << '\n';
    std::cout << std::endl;
}

}  // namespace

std::string normalize_size(std::string text) {
    text = trim(text);
    std::size_t i = 0;
    while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.')) ++i;
    if (i == 0) return {};
    const std::string number = text.substr(0, i);
    const std::string unit = lower(trim(text.substr(i)));
    if (!unit.empty() && unit != "g" && unit != "gb" && unit != "gib") return {};
    try {
        if (std::stod(number) < 1.0) return {};
    } catch (...) {
        return {};
    }
    return number + "GiB";
}

bool valid_hostname(const std::string& name) {
    if (name.empty() || name.size() > 63 || name.front() == '-' || name.back() == '-') return false;
    return std::all_of(name.begin(), name.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-'; });
}

bool valid_username(const std::string& name) {
    return !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
        return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '_' || c == '-';
    });
}

std::optional<Mode> choose_mode() {
    std::string notice;
    for (;;) {
        clear_screen();
        print_banner();
        box_top("MAIN MENU");
        const auto item = [](const char* key, const char* title, const char* text) {
            box_line("  " + paint(color::badge_ok, std::string(" ") + key + " ") + "  " + paint(color::white, fit(title, 28)) + paint(color::grey, text));
        };
        box_line("");
        item("1", "Install to a drive", "wipe it: EFI + tty host + container");
        box_line("");
        item("2", "Set up on this system", "installs Incus, SDDM, cage ...");
        box_line("");
        item("3", "Export this machine", "write a desktop image (.tar.zst)");
        box_line("");
        box_line("  " + paint(color::badge_bad, " Q ") + "  " + paint(color::white, "Quit"));
        box_line("");
        box_bottom();
        if (!notice.empty()) std::cout << '\n' << paint(color::red, "  ! " + notice) << '\n';
        std::cout << std::endl;

        const std::string choice = lower(ask("Choose (1-3, Q)"));
        if (input_closed() || choice == "q" || choice == "quit") return std::nullopt;
        if (choice == "1") return Mode::Install;
        if (choice == "2") return Mode::Running;
        if (choice == "3") return Mode::Export;
        notice = "Invalid choice: type 1, 2, 3 or Q.";
    }
}

bool settings(Mode mode, Options& o, Flags& flags) {
    // Sensible defaults, so only the real decisions are red.
    if (o.name.empty()) o.name = "desktop";
    if (mode == Mode::Export && o.export_path.empty()) o.export_path = "desktop.tar.zst";
    if (!flags.compress_given) {
        o.compression = "zstd:22";
        flags.compress_given = true;
    }

    std::string notice;
    for (;;) {
        std::vector<Row> rows = rows_for(mode, o, flags, notice);
        render(mode, rows, notice);
        notice.clear();

        const std::string command = lower(ask("Setting number, S to start, Q to quit"));
        if (input_closed() || command == "q" || command == "quit") return false;
        if (command == "s" || command == "start") {
            const bool missing = std::any_of(rows.begin(), rows.end(), [](const Row& r) { return r.required && r.value.empty(); });
            if (!missing) return true;
            notice = "Cannot start yet: fill in every setting that shows a red NOT SET box.";
            continue;
        }
        if (!command.empty() && command.size() <= 4 && std::all_of(command.begin(), command.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; })) {
            const std::size_t n = std::stoul(command);
            if (n >= 1 && n <= rows.size()) {
                rows[n - 1].edit();
                continue;
            }
        }
        notice = "Type a number from 1 to " + std::to_string(rows.size()) + ", S or Q.";
    }
}

}  // namespace cccu::menu
