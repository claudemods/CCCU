#include "mainwindow.hpp"

#include <QApplication>
#include <QStyleFactory>

#include <climits>
#include <cstdlib>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

// Started by a click (no terminal, no sudo): ask for administrator rights with the desktop's own password
// dialog by re-running this program through pkexec. The display variables are passed along because pkexec
// cleans the environment. When pkexec is missing or the dialog fails, the window still opens (Start stays
// disabled and says why).
void elevate(int argc, char** argv) {
    if (::geteuid() == 0 || std::getenv("CCCU_ELEVATED")) return;

    char self[PATH_MAX];
    const ssize_t length = ::readlink("/proc/self/exe", self, sizeof self - 1);
    if (length <= 0) return;
    self[length] = '\0';

    std::vector<std::string> command = {"pkexec", "env", "CCCU_ELEVATED=1"};
    for (const char* name : {"DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "XDG_SESSION_TYPE",
                             "XDG_CURRENT_DESKTOP", "QT_QPA_PLATFORM"}) {
        if (const char* value = std::getenv(name)) command.push_back(std::string(name) + "=" + value);
    }
    command.push_back(self);
    for (int i = 1; i < argc; ++i) command.push_back(argv[i]);

    std::vector<char*> args;
    for (auto& part : command) args.push_back(part.data());
    args.push_back(nullptr);
    ::execvp("pkexec", args.data());   // only returns when pkexec could not be started
}

}  // namespace

int main(int argc, char** argv) {
    elevate(argc, argv);

    QApplication app(argc, argv);
    QApplication::setApplicationName("CCCU");
    QApplication::setApplicationDisplayName("CCCU - Claudemods Container Creator Utility");
    QApplication::setDesktopFileName("cccu");
    QApplication::setStyle(QStyleFactory::create("Fusion"));

    MainWindow window;
    window.show();
    return app.exec();
}
