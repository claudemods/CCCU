#include "config.hpp"

namespace cccu {

std::optional<DisplayManager> parse_dm(std::string_view text) {
    if (text == "auto") return DisplayManager::Auto;
    if (text == "sddm") return DisplayManager::Sddm;
    if (text == "plasmalogin" || text == "plasma-login") return DisplayManager::PlasmaLogin;
    if (text == "none") return DisplayManager::None;
    return std::nullopt;
}

std::optional<Method> parse_method(std::string_view text) {
    if (text == "auto") return Method::Auto;
    if (text == "direct") return Method::Direct;
    if (text == "import") return Method::Import;
    return std::nullopt;
}

std::optional<Display> parse_display(std::string_view text) {
    if (text == "session") return Display::Session;
    if (text == "native") return Display::Native;
    return std::nullopt;
}

std::string_view to_string(Display display) { return display == Display::Native ? "native" : "session"; }

}  // namespace cccu
