#include "mainwindow.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include <pwd.h>
#include <unistd.h>

namespace {

// The home folder of the person who clicked: the GUI itself runs as root (pkexec), whose home is /root.
QString userHome() {
    const QByteArray uid = qgetenv("PKEXEC_UID");
    if (!uid.isEmpty()) {
        if (const passwd* pw = ::getpwuid(static_cast<uid_t>(uid.toUInt()))) return QString::fromLocal8Bit(pw->pw_dir);
    }
    return QDir::homePath();
}

// Paths typed by hand may be relative: make them absolute from the user's home, never from root's folder.
QString absolutize(const QString& text) {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty() || QDir::isAbsolutePath(trimmed)) return trimmed;
    return QDir(userHome()).absoluteFilePath(trimmed);
}

// Opens the log folder in the file manager. The GUI runs as root, which has no desktop session of its own,
// so the folder is opened as the person who clicked (runuser + their session bus).
void openLogFolder(QWidget* parent) {
    const QString dir = "/var/log/cccu";
    if (!QFileInfo(dir).isDir()) {
        QMessageBox::information(parent, "Log folder", "There is no log yet.\n\nLogs are written to " + dir +
                                 " when an operation runs (install.log, container.log, export.log, iso.log).");
        return;
    }
    bool started = false;
    const QByteArray uid = qgetenv("PKEXEC_UID");
    if (::geteuid() == 0 && !uid.isEmpty()) {
        if (const passwd* pw = ::getpwuid(static_cast<uid_t>(uid.toUInt()))) {
            const QString runtime = "/run/user/" + QString::fromLatin1(uid);
            QStringList args = {"-u", QString::fromLocal8Bit(pw->pw_name), "--", "env", "XDG_RUNTIME_DIR=" + runtime,
                                "DBUS_SESSION_BUS_ADDRESS=unix:path=" + runtime + "/bus"};
            for (const char* name : {"DISPLAY", "WAYLAND_DISPLAY", "XAUTHORITY"}) {
                if (qEnvironmentVariableIsSet(name)) args << QString("%1=%2").arg(QString::fromLatin1(name), qEnvironmentVariable(name));
            }
            args << "xdg-open" << dir;
            started = QProcess::startDetached("runuser", args);
        }
    } else {
        started = QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    }
    if (!started) QMessageBox::information(parent, "Log folder", "The logs are in:\n\n" + dir);
}

const char* kStyle = R"(
* { font-size: 13px; }
QMainWindow, QWidget#central { background: #12151a; }
QWidget { color: #e3e6ea; }
QLabel#brand { font-size: 26px; font-weight: 800; color: #ff5a5f; letter-spacing: 3px; }
QLabel#subtitle { color: #8b93a1; font-size: 13px; }
QLabel#version { color: #6b7280; font-size: 12px; }
QLabel#muted { color: #8b93a1; }
QLabel#h1 { font-size: 17px; font-weight: 700; color: #ffffff; }
QLabel#fieldLabel { color: #aab1be; }
QFrame#card { background: #1a1e25; border: 1px solid #272d38; border-radius: 10px; }
QTabWidget::pane { border: none; top: 0px; }
QTabBar { background: transparent; }
QTabBar::tab { background: transparent; color: #8b93a1; padding: 9px 20px; margin-right: 4px; border: none; border-bottom: 2px solid transparent; font-weight: 600; }
QTabBar::tab:selected { color: #ffffff; border-bottom: 2px solid #3b82f6; }
QTabBar::tab:hover:!selected { color: #cfd5de; }
QTabBar::tab:disabled { color: #4b5160; }
QLineEdit, QComboBox, QSpinBox { background: #0f1217; border: 1px solid #323a48; border-radius: 6px; padding: 5px 8px; min-height: 20px; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid #3b82f6; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled { color: #5b6270; background: #14171d; }
QComboBox QAbstractItemView { background: #0f1217; selection-background-color: #2a3550; border: 1px solid #323a48; }
QPushButton { background: #262d3a; border: 1px solid #343d4e; border-radius: 6px; padding: 6px 14px; }
QPushButton:hover { background: #303949; }
QPushButton:disabled { color: #5f6675; background: #1c2029; border-color: #272d38; }
QPushButton#start { background: #1f8f45; border: none; font-weight: 700; padding: 8px 30px; color: white; }
QPushButton#start:hover { background: #25a653; }
QPushButton#start:disabled { background: #25402f; color: #7d9a86; }
QPushButton#cancel { background: #8c2a2a; border: none; color: white; }
QPushButton#cancel:disabled { background: #2a2023; color: #6b5a5c; }
QPushButton#accent { background: #2f66c9; border: none; color: white; font-weight: 600; }
QPushButton#accent:hover { background: #3b78e6; }
QPushButton#accent:disabled { background: #232d44; color: #6a7388; }
QLabel[state="set"] { background: #1f8f45; color: white; border-radius: 4px; padding: 2px 0; font-weight: 700; font-size: 10px; }
QLabel[state="missing"] { background: #c62828; color: white; border-radius: 4px; padding: 2px 0; font-weight: 700; font-size: 10px; }
QLabel[state="optional"] { background: #323a48; color: #aab1be; border-radius: 4px; padding: 2px 0; font-weight: 700; font-size: 10px; }
QLabel#summary[state="ok"] { background: #1f8f45; color: white; border-radius: 12px; padding: 3px 14px; font-weight: 700; }
QLabel#summary[state="bad"] { background: #c62828; color: white; border-radius: 12px; padding: 3px 14px; font-weight: 700; }
QLabel#warning { background: #7a2e12; color: #ffd9c7; border-radius: 6px; padding: 6px 12px; }
QProgressBar { background: #0f1217; border: 1px solid #323a48; border-radius: 6px; text-align: center; min-height: 16px; max-height: 16px; font-size: 11px; }
QProgressBar::chunk { background: #1f8f45; border-radius: 5px; }
QPlainTextEdit { background: #0b0d11; border: 1px solid #272d38; border-radius: 6px; padding: 4px; }
QTableWidget { background: #0f1217; border: 1px solid #323a48; border-radius: 6px; gridline-color: #232935; outline: none; }
QTableWidget::item { padding: 6px 8px; }
QTableWidget::item:selected { background: #2a3550; color: white; }
QHeaderView::section { background: #1a1e25; color: #8b93a1; border: none; border-bottom: 1px solid #272d38; padding: 6px 8px; font-weight: 600; }
QCheckBox::indicator { width: 16px; height: 16px; }
QToolTip { background: #0f1217; color: #e3e6ea; border: 1px solid #323a48; }
)";

bool validUser(const QString& text) {
    static const QRegularExpression re("^[a-z0-9_-]+$");
    return re.match(text).hasMatch();
}
bool validName(const QString& text) {
    static const QRegularExpression re("^[A-Za-z][A-Za-z0-9-]{0,62}$");
    return re.match(text).hasMatch() && !text.endsWith('-');
}
bool validHostname(const QString& text) {
    static const QRegularExpression re("^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$");
    return re.match(text).hasMatch();
}
bool validWord(const QString& text) {   // ISO name / tag: letters, digits, . _ -
    static const QRegularExpression re("^[A-Za-z0-9._-]+$");
    return re.match(text).hasMatch();
}

void setBadge(QLabel* badge, const char* state, const QString& text) {
    badge->setText(text);
    if (badge->property("state").toString() == state) return;
    badge->setProperty("state", state);
    badge->style()->unpolish(badge);
    badge->style()->polish(badge);
}

QLabel* makeLabel(const QString& text, const char* objectName) {
    auto* label = new QLabel(text);
    label->setObjectName(objectName);
    return label;
}

}  // namespace

MainWindow::MainWindow() {
    setWindowTitle("CCCU - Claudemods Container Creator Utility");
    resize(1200, 780);
    setMinimumSize(1120, 740);
    setStyleSheet(kStyle);

    auto* central = new QWidget;
    central->setObjectName("central");
    setCentralWidget(central);
    auto* col = new QVBoxLayout(central);
    col->setContentsMargins(20, 14, 20, 14);
    col->setSpacing(10);

    // ---- header ----
    auto* header = new QHBoxLayout;
    header->setSpacing(14);
    header->addWidget(makeLabel("CCCU", "brand"));
    header->addWidget(makeLabel("Claudemods Container Creator Utility", "subtitle"));
    header->addStretch();
    rootWarning_ = makeLabel("Administrator rights missing - close and open CCCU again (is polkit / pkexec installed?)", "warning");
    rootWarning_->setVisible(::geteuid() != 0);
    header->addWidget(rootWarning_);
    header->addWidget(makeLabel(QString("v%1  %2").arg(QString::fromLatin1(CCCU_GUI_VERSION), QString::fromLatin1(CCCU_DATE)), "version"));
    col->addLayout(header);

    // ---- tabs: one settings page each ----
    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    tabs_->addTab(buildPage(Install), "Install to a drive");
    tabs_->addTab(buildPage(Running), "Set up on this system");
    tabs_->addTab(buildPage(Export), "Export");
    tabs_->addTab(buildIsoPage(), "Create ISO");
    tabs_->addTab(buildContainersPage(), "Containers");
    col->addWidget(tabs_, 1);

    // ---- bottom: progress, log, buttons ----
    auto* bottom = new QFrame;
    bottom->setObjectName("card");
    auto* b = new QVBoxLayout(bottom);
    b->setContentsMargins(16, 10, 16, 10);
    b->setSpacing(6);

    auto* top = new QHBoxLayout;
    stepLabel_ = makeLabel("Ready.", "muted");
    summary_ = makeLabel("", "summary");
    top->addWidget(stepLabel_, 1);
    top->addWidget(summary_);
    b->addLayout(top);

    bar_ = new QProgressBar;
    bar_->setRange(0, 1);
    bar_->setValue(0);
    bar_->setFormat("%p%");
    b->addWidget(bar_);

    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(20000);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setFixedHeight(112);
    log_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // follows the output by itself; no scroller on screen
    log_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    log_->setLineWrapMode(QPlainTextEdit::NoWrap);
    b->addWidget(log_);

    auto* actions = new QHBoxLayout;
    result_ = new QLabel;
    result_->setObjectName("muted");
    dryRun_ = new QCheckBox("Dry run (change nothing)");
    auto* openLog = new QPushButton("Open log folder");
    cancelButton_ = new QPushButton("Cancel");
    cancelButton_->setObjectName("cancel");
    cancelButton_->setEnabled(false);
    startButton_ = new QPushButton("Start");
    startButton_->setObjectName("start");
    actions->addWidget(result_, 1);
    actions->addWidget(dryRun_);
    actions->addSpacing(8);
    actions->addWidget(openLog);
    actions->addWidget(cancelButton_);
    actions->addWidget(startButton_);
    b->addLayout(actions);
    col->addWidget(bottom);

    connect(tabs_, &QTabWidget::currentChanged, this, [this](int) { refreshStatus(); });
    connect(startButton_, &QPushButton::clicked, this, [this] { start(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this] { cancel(); });
    connect(openLog, &QPushButton::clicked, this, [this] { openLogFolder(this); });

    refreshContainers();
    refreshStatus();
}

MainWindow::Mode MainWindow::currentMode() const {
    return static_cast<Mode>(std::clamp(tabs_->currentIndex(), 0, kModes - 1));
}

// ---- pages ------------------------------------------------------------------------------------

// A card with a title, a line of explanation and the two-column grid the settings go into.
QWidget* MainWindow::card(const QString& title, const QString& text, QWidget**, Mode mode) {
    auto* frame = new QFrame;
    frame->setObjectName("card");
    auto* v = new QVBoxLayout(frame);
    v->setContentsMargins(20, 14, 20, 14);
    v->setSpacing(4);
    v->addWidget(makeLabel(title, "h1"));
    auto* description = makeLabel(text, "muted");
    description->setWordWrap(true);
    v->addWidget(description);

    auto* host = new QWidget;
    auto* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 10, 0, 0);
    grid->setHorizontalSpacing(30);
    grid->setVerticalSpacing(9);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    data_[mode].grid = grid;
    v->addWidget(host);
    v->addStretch();
    return frame;
}

QWidget* MainWindow::buildPage(Mode mode) {
    static const char* titles[] = {"Install to a drive", "Set up on this system", "Export this machine"};
    static const char* texts[] = {
        "Wipes the drive: EFI boot partition, tty host system with Incus, and your desktop container (created on the first boot).",
        "Installs what this machine needs (Incus, SDDM, cage ...) with its own package manager, then creates the container. Reboot afterwards.",
        "Writes this running system as a desktop image that can be cloned on another machine."};

    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 10, 0, 0);
    outer->addWidget(card(titles[mode], texts[mode], nullptr, mode));

    const QString imageFilter = "System images (*.tar *.tar.zst *.tar.xz *.tar.gz *.squashfs *.sfs *.erofs);;All files (*)";
    const auto isFile = [](QLineEdit* edit) { return [edit] { return QFileInfo(absolutize(edit->text())).isFile(); }; };

    if (mode == Install) {
        // The target: a whole drive (wiped), or two EXISTING partitions next to another system (only those are formatted).
        targetMode_ = new QComboBox;
        targetMode_->addItem("Wipe a whole drive", "drive");
        targetMode_->addItem("Two existing partitions (next to another system)", "partitions");
        addRow(Install, "Install to", targetMode_, {targetMode_}, true, [] { return true; });
        const auto drivesMode = [this] { return targetMode_->currentData().toString() == "drive"; };

        const auto comboWithRefresh = [this](QComboBox* box, const std::function<void()>& refresh) {
            auto* row = new QWidget;
            auto* layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            auto* button = new QPushButton("Refresh");
            layout->addWidget(box, 1);
            layout->addWidget(button);
            connect(button, &QPushButton::clicked, this, [refresh] { refresh(); });
            return row;
        };

        drive_ = new QComboBox;
        QWidget* driveCell = addRow(Install, "Target drive (WIPED)", comboWithRefresh(drive_, [this] { refreshDrives(drive_); }), {drive_}, true,
                                    [this, drivesMode] { return !drivesMode() || !drive_->currentData().toString().isEmpty(); });
        refreshDrives(drive_);

        bootPart_ = new QComboBox;
        QWidget* bootCell = addRow(Install, "Boot partition (FAT32)", comboWithRefresh(bootPart_, [this] { refreshPartitions(bootPart_); }), {bootPart_}, true,
                                   [this, drivesMode] { return drivesMode() || !bootPart_->currentData().toString().isEmpty(); });
        rootPart_ = new QComboBox;
        QWidget* rootCell = addRow(Install, "Root partition (system)", comboWithRefresh(rootPart_, [this] { refreshPartitions(rootPart_); }), {rootPart_}, true,
                                   [this, drivesMode] {
                                       return drivesMode() || (!rootPart_->currentData().toString().isEmpty() &&
                                                               rootPart_->currentData() != bootPart_->currentData());
                                   });
        connect(bootPart_, &QComboBox::currentIndexChanged, this, [this] { refreshStatus(); });
        keepBoot_ = new QCheckBox("Keep it as it is (already FAT32)");
        keepBoot_->setToolTip("Off: the boot partition is formatted FAT32. Only these two partitions are touched.");
        QWidget* keepCell = addRow(Install, "Boot partition data", keepBoot_, {keepBoot_}, false, [this] { return keepBoot_->isChecked(); });
        refreshPartitions(bootPart_);
        refreshPartitions(rootPart_);
        bootPart_->setCurrentIndex(-1);
        rootPart_->setCurrentIndex(-1);

        const auto applyTarget = [=] {
            const bool drivesOnly = drivesMode();
            driveCell->setVisible(drivesOnly);
            bootCell->setVisible(!drivesOnly);
            rootCell->setVisible(!drivesOnly);
            keepCell->setVisible(!drivesOnly);
        };
        connect(targetMode_, &QComboBox::currentIndexChanged, this, [applyTarget] { applyTarget(); });
        applyTarget();

        installImage_ = new QLineEdit;
        installImage_->setPlaceholderText("Desktop image made with --export");
        addRow(Install, "Desktop image", fileRow(installImage_, false, imageFilter), {installImage_}, true, isFile(installImage_));

        hostSource_ = new QComboBox;
        hostSource_->addItems({"Live ISO you booted (default)", "Clone of this running system", "Image file ...", "ISO file ...",
                               "Base package list"});
        hostImage_ = new QLineEdit;
        hostImage_->setPlaceholderText("tty host system image or ISO");
        // The base lists are built in: pick one, nothing to type. The tooltip shows what it installs.
        hostPreset_ = new QComboBox;
        hostPreset_->addItem("Arch Linux  (pacstrap)", "arch");
        hostPreset_->addItem("Debian stable  (debootstrap)", "debian");
        hostPreset_->addItem("Ubuntu 24.04 LTS  (debootstrap)", "ubuntu");
        hostPreset_->addItem("Fedora, same release as the live system  (dnf)", "fedora");
        hostPreset_->setToolTip("Arch: base linux linux-firmware networkmanager sudo nano\n"
                                "Debian / Ubuntu: kernel, systemd, dbus, udev, locales, network-manager, sudo, nano, initramfs-tools ...\n"
                                "Fedora: @core kernel dracut NetworkManager sudo nano (needs a Fedora live system)");
        hostPreset_->setEnabled(false);
        auto* hostRow = new QWidget;
        auto* hostLayout = new QHBoxLayout(hostRow);
        hostLayout->setContentsMargins(0, 0, 0, 0);
        hostLayout->addWidget(hostSource_, 1);
        hostLayout->addWidget(fileRow(hostImage_, false, imageFilter + ";;ISO files (*.iso)"), 2);
        hostLayout->addWidget(hostPreset_, 1);
        hostImage_->parentWidget()->setEnabled(false);
        connect(hostSource_, &QComboBox::currentIndexChanged, this, [this](int index) {
            hostImage_->parentWidget()->setEnabled(index == 2 || index == 3);   // image file or ISO file
            hostPreset_->setEnabled(index == 4);                                // base package list
        });
        addRow(Install, "tty host system", hostRow, {hostSource_, hostImage_, hostPreset_}, true,
               [this, f = isFile(hostImage_)] { return hostSource_->currentIndex() < 2 || hostSource_->currentIndex() == 4 || f(); }, true);

        hostname_ = new QLineEdit("cccu");
        addRow(Install, "Hostname", hostname_, {hostname_}, true, [this] { return validHostname(hostname_->text().trimmed()); });
        buildCommon(Install);
    } else if (mode == Running) {
        runSource_ = new QComboBox;
        runSource_->addItems({"Image file", "Clone THIS running system"});
        runImage_ = new QLineEdit;
        runImage_->setPlaceholderText("System image made with --export");
        auto* sourceRow = new QWidget;
        auto* sourceLayout = new QHBoxLayout(sourceRow);
        sourceLayout->setContentsMargins(0, 0, 0, 0);
        sourceLayout->addWidget(runSource_, 1);
        sourceLayout->addWidget(fileRow(runImage_, false, imageFilter), 2);
        connect(runSource_, &QComboBox::currentIndexChanged, this, [this](int index) { runImage_->parentWidget()->setEnabled(index == 0); });
        addRow(Running, "System to clone", sourceRow, {runSource_, runImage_}, true,
               [this, f = isFile(runImage_)] { return runSource_->currentIndex() == 1 || f(); }, true);
        buildCommon(Running);
    } else {
        exportPath_ = new QLineEdit(userHome() + "/desktop.tar.zst");
        addRow(Export, "Output file", fileRow(exportPath_, true, "Images (*.tar *.tar.zst *.tar.xz *.tar.gz)"), {exportPath_}, true,
               [this] { return !exportPath_->text().trimmed().isEmpty(); }, true);
    }
    outer->addStretch();
    return page;
}

void MainWindow::buildCommon(Mode mode) {
    Common& c = common_[mode];

    c.user = new QLineEdit;
    c.user->setPlaceholderText("lowercase letters, digits, - and _");
    addRow(mode, "User on the tty host", c.user, {c.user}, true, [u = c.user] { return validUser(u->text().trimmed()); });

    c.pass = new QLineEdit;
    c.pass->setEchoMode(QLineEdit::Password);
    c.pass->setPlaceholderText(mode == Install ? "Password of the new user" : "Only used when the user is new");
    addRow(mode, "Password of that user", c.pass, {c.pass}, mode == Install, [p = c.pass] { return !p->text().isEmpty(); });

    if (mode == Install) {
        rootPass_ = new QLineEdit;
        rootPass_->setEchoMode(QLineEdit::Password);
        rootPass_->setPlaceholderText("Root password of the new host");
        addRow(Install, "Root password", rootPass_, {rootPass_}, true, [this] { return !rootPass_->text().isEmpty(); });
    }

    c.containerUser = new QLineEdit;
    c.containerUser->setPlaceholderText("Same as the host user");
    addRow(mode, "User in the clone", c.containerUser, {c.containerUser, c.user}, true,
           [u = c.user, cu = c.containerUser] { return validUser((cu->text().trimmed().isEmpty() ? u->text() : cu->text()).trimmed()); });

    if (mode == Install) {
        fs_ = new QComboBox;
        fs_->addItems({"ext4", "btrfs"});
        addRow(Install, "System filesystem", fs_, {fs_}, true, [] { return true; });
    }

    c.name = new QLineEdit("desktop");
    addRow(mode, "Container name", c.name, {c.name}, true, [n = c.name] { return validName(n->text().trimmed()); });

    c.size = new QSpinBox;
    c.size->setRange(0, 100000);
    c.size->setSuffix(" GiB");
    c.size->setSpecialValueText("not set");
    addRow(mode, "Container size", c.size, {c.size}, true, [s = c.size] { return s->value() >= 1; });

    c.compress = new QComboBox;
    c.compress->addItems({"zstd:22", "zstd:15", "zstd:9", "zstd:3", "zlib:9", "lzo", "none"});
    addRow(mode, "Compression", c.compress, {c.compress}, true, [] { return true; });

    c.immutable = new QCheckBox("Mount the system read-only");
    addRow(mode, "Immutable container", c.immutable, {c.immutable}, false, [i = c.immutable] { return i->isChecked(); });
}

// The ISO tab: exactly what the claudebackup ISO creator offers, plus writing the result to a USB stick.
QWidget* MainWindow::buildIsoPage() {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 10, 0, 0);
    auto* row = new QHBoxLayout;
    row->setSpacing(12);

    row->addWidget(card("Create a live ISO",
                        "A bootable live ISO of the container's system, built INSIDE the container with the claudebackup engine "
                        "(Arch, Debian, Fedora and openSUSE families). Users, settings and /home are cloned as they are. "
                        "Build ISO sets it up as a service: after the reboot it runs on the console with the progress display, "
                        "then the login screen appears.",
                        nullptr, Iso), 3);

    isoContainer_ = new QComboBox;
    isoContainer_->setEditable(true);
    isoContainer_->setToolTip("The container whose system becomes the ISO");
    addRow(Iso, "Build from container", isoContainer_, {isoContainer_}, true, [this] { return validName(isoContainer_->currentText().trimmed()); });

    isoOut_ = new QLineEdit(userHome() + "/Downloads");
    addRow(Iso, "ISO folder", fileRow(isoOut_, true, QString(), true), {isoOut_}, true, [this] { return !isoOut_->text().trimmed().isEmpty(); });

    isoName_ = new QLineEdit;
    isoName_->setPlaceholderText("automatic: claudebackup-<distro>-<date>.iso");
    addRow(Iso, "ISO name", isoName_, {isoName_}, false, [this] {
        return !isoName_->text().trimmed().isEmpty() && validWord(isoName_->text().trimmed());
    });

    isoTag_ = new QLineEdit;
    isoTag_->setPlaceholderText("default 2026 (Arch volume label)");
    addRow(Iso, "ISO tag", isoTag_, {isoTag_}, false, [this] { return !isoTag_->text().trimmed().isEmpty() && validWord(isoTag_->text().trimmed()); });

    isoFormat_ = new QComboBox;
    isoFormat_->addItems({"squashfs", "erofs"});
    isoFormat_->setToolTip("erofs needs a kernel with erofs support inside the container");
    addRow(Iso, "Image format", isoFormat_, {isoFormat_}, true, [] { return true; });

    isoLevel_ = new QSpinBox;
    isoLevel_->setRange(1, 22);
    isoLevel_->setValue(3);
    isoLevel_->setToolTip("zstd level: higher = smaller and slower");
    addRow(Iso, "zstd level (1-22)", isoLevel_, {isoLevel_}, true, [] { return true; });

    isoInstaller_ = new QComboBox;
    isoInstaller_->addItem("Calamares, ext4", "ext4");
    isoInstaller_->addItem("Calamares, btrfs zstd:22", "btrfs");
    isoInstaller_->addItem("No installer", "none");
    addRow(Iso, "Installer on the ISO", isoInstaller_, {isoInstaller_}, true, [] { return true; });

    isoRam_ = new QCheckBox("Add a copy-to-RAM boot entry");
    isoRam_->setChecked(true);
    addRow(Iso, "RAM mode", isoRam_, {isoRam_}, false, [this] { return isoRam_->isChecked(); });

    isoCache_ = new QCheckBox("Leave package caches out");
    isoCache_->setChecked(true);
    addRow(Iso, "Clean caches", isoCache_, {isoCache_}, false, [this] { return isoCache_->isChecked(); });


    // ---- write to USB ----
    auto* usb = new QFrame;
    usb->setObjectName("card");
    usb->setFixedWidth(360);
    auto* u = new QVBoxLayout(usb);
    u->setContentsMargins(20, 14, 20, 14);
    u->setSpacing(6);
    u->addWidget(makeLabel("Write an ISO to USB", "h1"));
    auto* usbText = makeLabel("Erases the whole target drive. Test the ISO in a VM (UEFI) first.", "muted");
    usbText->setWordWrap(true);
    u->addWidget(usbText);
    usbIso_ = new QLineEdit;
    usbIso_->setPlaceholderText("ISO file");
    u->addSpacing(6);
    u->addWidget(makeLabel("ISO file", "fieldLabel"));
    u->addWidget(fileRow(usbIso_, false, "ISO files (*.iso);;All files (*)"));
    u->addWidget(makeLabel("Target drive (WIPED)", "fieldLabel"));
    usbDrive_ = new QComboBox;
    auto* usbDriveRow = new QWidget;
    auto* usbDriveLayout = new QHBoxLayout(usbDriveRow);
    usbDriveLayout->setContentsMargins(0, 0, 0, 0);
    auto* usbRefresh = new QPushButton("Refresh");
    usbDriveLayout->addWidget(usbDrive_, 1);
    usbDriveLayout->addWidget(usbRefresh);
    u->addWidget(usbDriveRow);
    auto* write = new QPushButton("Write to USB");
    write->setObjectName("accent");
    u->addSpacing(6);
    u->addWidget(write);
    u->addStretch();
    refreshDrives(usbDrive_);
    connect(usbRefresh, &QPushButton::clicked, this, [this] { refreshDrives(usbDrive_); });
    connect(write, &QPushButton::clicked, this, [this] {
        const QString iso = absolutize(usbIso_->text());
        const QString drive = usbDrive_->currentData().toString();
        if (!QFileInfo(iso).isFile() || drive.isEmpty()) {
            QMessageBox::information(this, "Write to USB", "Choose an ISO file and a target drive first.");
            return;
        }
        bool accepted = false;
        const QString typed = QInputDialog::getText(this, "Confirm the wipe",
                                                    QString("ALL DATA on %1 will be destroyed.\n\nType the device path to confirm:").arg(drive),
                                                    QLineEdit::Normal, QString(), &accepted);
        if (!accepted || typed.trimmed() != drive) return;
        runCli({"iso-usb", "--iso-file", iso, "--drive", drive, "--yes"}, Iso, "Writing the ISO to " + drive);
    });
    row->addWidget(usb, 0);

    outer->addLayout(row);
    outer->addStretch();
    return page;
}

// The Containers tab: what exists, which one the host session shows, duplicate and switch.
QWidget* MainWindow::buildContainersPage() {
    auto* page = new QWidget;
    auto* outer = new QHBoxLayout(page);
    outer->setContentsMargins(0, 10, 0, 0);
    outer->setSpacing(12);

    auto* list = new QFrame;
    list->setObjectName("card");
    auto* l = new QVBoxLayout(list);
    l->setContentsMargins(20, 14, 20, 14);
    l->setSpacing(6);
    l->addWidget(makeLabel("Containers", "h1"));
    containerHint_ = makeLabel("The ACTIVE container is the one the host session (Container Plasma) shows.", "muted");
    containerHint_->setWordWrap(true);
    l->addWidget(containerHint_);
    table_ = new QTableWidget(0, 3);
    table_->setHorizontalHeaderLabels({"Name", "State", "Host session"});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setShowGrid(false);
    table_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    l->addWidget(table_, 1);
    outer->addWidget(list, 3);

    auto* side = new QFrame;
    side->setObjectName("card");
    side->setFixedWidth(360);
    auto* s = new QVBoxLayout(side);
    s->setContentsMargins(20, 14, 20, 14);
    s->setSpacing(6);
    s->addWidget(makeLabel("Selected container", "h1"));

    auto* buttons = new QGridLayout;
    auto* switchButton = new QPushButton("Switch to it");
    switchButton->setObjectName("accent");
    auto* startB = new QPushButton("Start");
    auto* stopB = new QPushButton("Stop");
    auto* refreshB = new QPushButton("Refresh");
    buttons->addWidget(switchButton, 0, 0, 1, 2);
    buttons->addWidget(startB, 1, 0);
    buttons->addWidget(stopB, 1, 1);
    buttons->addWidget(refreshB, 2, 0, 1, 2);
    s->addLayout(buttons);

    s->addSpacing(8);
    s->addWidget(makeLabel("Duplicate it", "h1"));
    auto* dupText = makeLabel("A full, independent copy (incus copy). Immutable containers are not supported yet.", "muted");
    dupText->setWordWrap(true);
    s->addWidget(dupText);
    s->addWidget(makeLabel("Name of the copy", "fieldLabel"));
    copyName_ = new QLineEdit;
    copyName_->setPlaceholderText("e.g. desktop-test");
    s->addWidget(copyName_);
    copySwitch_ = new QCheckBox("Switch to the copy afterwards");
    s->addWidget(copySwitch_);
    auto* duplicateButton = new QPushButton("Duplicate");
    duplicateButton->setObjectName("accent");
    s->addWidget(duplicateButton);
    s->addStretch();
    outer->addWidget(side, 0);

    connect(refreshB, &QPushButton::clicked, this, [this] { refreshContainers(); });
    connect(switchButton, &QPushButton::clicked, this, [this] {
        const QString name = selectedContainer();
        if (name.isEmpty()) return;
        runCli({"switch", name, "--yes"}, Containers, "Switching to " + name);
    });
    connect(startB, &QPushButton::clicked, this, [this] {
        const QString name = selectedContainer();
        if (!name.isEmpty()) runCli({"start", name, "--yes"}, Containers, "Starting " + name);
    });
    connect(stopB, &QPushButton::clicked, this, [this] {
        const QString name = selectedContainer();
        if (!name.isEmpty()) runCli({"stop", name, "--yes"}, Containers, "Stopping " + name);
    });
    connect(duplicateButton, &QPushButton::clicked, this, [this] {
        const QString source = selectedContainer();
        const QString copy = copyName_->text().trimmed();
        if (source.isEmpty()) {
            QMessageBox::information(this, "Duplicate", "Select the container to duplicate first.");
            return;
        }
        if (!validName(copy)) {
            QMessageBox::information(this, "Duplicate", "Give the copy a name: letters, digits and '-', starting with a letter.");
            return;
        }
        QStringList args = {"duplicate", source, copy, "--yes"};
        if (copySwitch_->isChecked()) args << "--switch";
        runCli(args, Containers, "Duplicating " + source + " as " + copy);
    });
    return page;
}

QWidget* MainWindow::addRow(Mode mode, const QString& label, QWidget* shown, const QList<QWidget*>& watched, bool required,
                            std::function<bool()> isSet, bool wide) {
    PageData& page = data_[mode];
    auto* cell = new QWidget;
    auto* h = new QHBoxLayout(cell);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(10);
    auto* name = makeLabel(label, "fieldLabel");
    name->setFixedWidth(150);
    auto* badge = new QLabel;
    badge->setFixedWidth(66);
    badge->setAlignment(Qt::AlignCenter);
    h->addWidget(name);
    h->addWidget(shown, 1);
    h->addWidget(badge);

    if (wide && page.cell % 2 == 1) ++page.cell;
    page.grid->addWidget(cell, page.cell / 2, page.cell % 2, 1, wide ? 2 : 1);
    page.cell += wide ? 2 : 1;
    page.fields.push_back({badge, required, std::move(isSet)});

    for (QWidget* w : watched) {
        if (auto* e = qobject_cast<QLineEdit*>(w)) connect(e, &QLineEdit::textChanged, this, [this] { refreshStatus(); });
        else if (auto* c = qobject_cast<QComboBox*>(w)) {
            connect(c, &QComboBox::currentIndexChanged, this, [this] { refreshStatus(); });
            if (c->isEditable()) connect(c, &QComboBox::editTextChanged, this, [this] { refreshStatus(); });
        } else if (auto* b = qobject_cast<QCheckBox*>(w)) connect(b, &QCheckBox::toggled, this, [this] { refreshStatus(); });
        else if (auto* s = qobject_cast<QSpinBox*>(w)) connect(s, &QSpinBox::valueChanged, this, [this] { refreshStatus(); });
    }
    return cell;
}

// Existing partitions (for installing next to another system): name, size, file system and label.
void MainWindow::refreshPartitions(QComboBox* box) {
    box->clear();
    QProcess lsblk;
    lsblk.start("lsblk", {"-pnP", "-o", "NAME,SIZE,FSTYPE,LABEL,TYPE"});
    if (!lsblk.waitForFinished(4000)) return;
    static const QRegularExpression pair(R"((\w+)="([^"]*)")");
    for (const QString& line : QString::fromUtf8(lsblk.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts)) {
        QMap<QString, QString> f;
        for (auto it = pair.globalMatch(line); it.hasNext();) {
            const auto m = it.next();
            f[m.captured(1)] = m.captured(2);
        }
        if (f.value("TYPE") != "part") continue;
        QString label = QString("%1   %2   %3   %4").arg(f.value("NAME"), f.value("SIZE"), f.value("FSTYPE"), f.value("LABEL")).trimmed();
        label.replace("\\x20", " ");
        box->addItem(label, f.value("NAME"));
    }
    box->setCurrentIndex(-1);
}

QWidget* MainWindow::fileRow(QLineEdit* edit, bool save, const QString& filter, bool folder) {
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* browse = new QPushButton("Browse");
    layout->addWidget(edit, 1);
    layout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this, edit, save, filter, folder] {
        const QString start = edit->text().trimmed().isEmpty() ? userHome() : absolutize(edit->text());
        QString path;
        if (folder) path = QFileDialog::getExistingDirectory(this, "Choose a folder", start);
        else if (save) path = QFileDialog::getSaveFileName(this, "Where should the image be saved?", start, filter);
        else path = QFileDialog::getOpenFileName(this, "Choose a file", start, filter);
        if (!path.isEmpty()) edit->setText(path);
    });
    return row;
}

void MainWindow::refreshDrives(QComboBox* box) {
    box->clear();
    QProcess lsblk;
    lsblk.start("lsblk", {"-dnpr", "-e", "1,7,11", "-o", "NAME,SIZE,MODEL"});
    if (!lsblk.waitForFinished(4000)) return;
    const QStringList lines = QString::fromUtf8(lsblk.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QStringList parts = line.split(' ');
        if (parts.size() < 2 || parts[0].contains("zram")) continue;
        QString model = parts.mid(2).join(' ');
        model.replace("\\x20", " ");
        box->addItem(QString("%1   %2   %3").arg(parts[0], parts[1], model.trimmed()), parts[0]);
    }
    box->setCurrentIndex(-1);
}

// Asks the tool for the containers (name, state, active) and fills the table and the ISO tab's list.
void MainWindow::refreshContainers() {
    if (!table_ || !isoContainer_) return;
    QStringList names;
    table_->setRowCount(0);
    if (::geteuid() == 0) {
        QProcess p;
        p.start(backendPath(), {"containers", "--plain"});
        if (p.waitForFinished(8000) && p.exitCode() == 0) {
            for (const QString& line : QString::fromUtf8(p.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts)) {
                const QStringList f = line.split('\t');
                if (f.size() < 2) continue;
                const int row = table_->rowCount();
                table_->insertRow(row);
                table_->setItem(row, 0, new QTableWidgetItem(f[0]));
                table_->setItem(row, 1, new QTableWidgetItem(f[1].toLower()));
                table_->setItem(row, 2, new QTableWidgetItem(f.value(2) == "active" ? "ACTIVE" : ""));
                names << f[0];
            }
        }
    }
    if (table_->rowCount() > 0 && table_->currentRow() < 0) table_->selectRow(0);

    const QString keep = isoContainer_->currentText();
    isoContainer_->blockSignals(true);
    isoContainer_->clear();
    isoContainer_->addItems(names.isEmpty() ? QStringList{"desktop"} : names);
    isoContainer_->setCurrentText(keep.isEmpty() ? isoContainer_->itemText(0) : keep);
    isoContainer_->blockSignals(false);
    refreshStatus();
}

QString MainWindow::selectedContainer() const {
    const int row = table_->currentRow();
    return row < 0 ? QString() : table_->item(row, 0)->text();
}

// ---- status -----------------------------------------------------------------------------------

int MainWindow::missing(Mode mode) const {
    int count = 0;
    for (const Field& f : data_[mode].fields) {
        if (f.required && !f.isSet()) ++count;
    }
    return count;
}

void MainWindow::refreshStatus() {
    if (!startButton_ || !summary_ || !tabs_) return;   // the window is still being built
    for (const PageData& page : data_) {
        for (const Field& f : page.fields) {
            if (f.isSet()) setBadge(f.badge, "set", "SET");
            else if (f.required) setBadge(f.badge, "missing", "NOT SET");
            else setBadge(f.badge, "optional", "OPTIONAL");
        }
    }
    const Mode mode = currentMode();
    static const char* startText[] = {"Install", "Set up", "Export", "Set up ISO build", ""};
    const bool hasStart = mode != Containers;
    startButton_->setVisible(hasStart);
    summary_->setVisible(hasStart);
    startButton_->setText(startText[mode]);
    const int n = hasStart ? missing(mode) : 0;
    summary_->setText(n == 0 ? "READY" : QString("%1 required setting%2 not set").arg(n).arg(n == 1 ? "" : "s"));
    summary_->setProperty("state", n == 0 ? "ok" : "bad");
    summary_->style()->unpolish(summary_);
    summary_->style()->polish(summary_);
    startButton_->setEnabled(n == 0 && !running_ && ::geteuid() == 0);
}

// ---- running the backend ----------------------------------------------------------------------

QString MainWindow::backendPath() const {
    const QString next = QCoreApplication::applicationDirPath() + "/cccu";
    return QFileInfo(next).isExecutable() ? next : QString("cccu");
}

QStringList MainWindow::buildArgs(Mode mode) const {
    QStringList args;
    if (mode == Export) {
        args << "--export" << absolutize(exportPath_->text());
    } else if (mode == Iso) {
        args << "iso" << "--name" << isoContainer_->currentText().trimmed() << "--iso-out" << absolutize(isoOut_->text())
             << "--iso-format" << isoFormat_->currentText() << "--iso-level" << QString::number(isoLevel_->value());
        if (!isoName_->text().trimmed().isEmpty()) args << "--iso-name" << isoName_->text().trimmed();
        if (!isoTag_->text().trimmed().isEmpty()) args << "--iso-tag" << isoTag_->text().trimmed();
        if (!isoRam_->isChecked()) args << "--no-ram";
        if (!isoCache_->isChecked()) args << "--keep-pkg-cache";
        const QString installer = isoInstaller_->currentData().toString();
        if (installer == "none") args << "--no-installer";
        else if (installer == "btrfs") args << "--installer-btrfs";
        args << "--on-reboot";   // the window only ever sets the build up: the service builds the ISO after the reboot
    } else {
        const Common& c = common_[mode];
        const QString user = c.user->text().trimmed();
        const QString containerUser = c.containerUser->text().trimmed().isEmpty() ? user : c.containerUser->text().trimmed();
        if (mode == Install) {
            args << "install";
            if (targetMode_->currentData().toString() == "partitions") {
                args << "--boot-part" << bootPart_->currentData().toString() << "--root-part" << rootPart_->currentData().toString();
                if (keepBoot_->isChecked()) args << "--keep-boot";
            } else {
                args << "--drive" << drive_->currentData().toString();
            }
            args << "--image" << absolutize(installImage_->text()) << "--hostname" << hostname_->text().trimmed() << "--fs" << fs_->currentText();
            if (hostSource_->currentIndex() == 1) args << "--clone-system";
            if (hostSource_->currentIndex() == 2) args << "--host-image" << absolutize(hostImage_->text());
            if (hostSource_->currentIndex() == 3) args << "--host-iso" << absolutize(hostImage_->text());
            if (hostSource_->currentIndex() == 4) args << "--host-bootstrap" << hostPreset_->currentData().toString();
        } else {
            args << "setup";
            if (runSource_->currentIndex() == 1) args << "--clone-host";
            else args << "--image" << absolutize(runImage_->text());
        }
        args << "--user" << user << "--container-user" << containerUser << "--name" << c.name->text().trimmed() << "--size"
             << QString("%1GiB").arg(c.size->value()) << "--compress" << c.compress->currentText();
        if (c.immutable->isChecked()) args << "--immutable";
    }
    if (dryRun_->isChecked()) args << "--dry-run";
    args << "--yes";
    return args;
}

void MainWindow::start() {
    const Mode mode = currentMode();
    if (mode == Containers || missing(mode) != 0 || running_) return;

    if (mode == Install) {
        const bool partitions = targetMode_->currentData().toString() == "partitions";
        const QString drive = partitions ? rootPart_->currentData().toString() : drive_->currentData().toString();
        const QString warning = partitions
            ? QString("The ROOT partition %1 will be formatted%2.\nOnly these two partitions are touched; the rest of the disk stays as it is.\n\n"
                      "Type the ROOT partition path to confirm:").arg(drive, keepBoot_->isChecked() ? "" : QString(" and so will the boot partition %1").arg(bootPart_->currentData().toString()))
            : QString("ALL DATA on %1 will be destroyed.\n\nType the device path to confirm:").arg(drive);
        bool accepted = false;
        const QString typed = QInputDialog::getText(this, "Confirm", warning, QLineEdit::Normal, QString(), &accepted);
        if (!accepted || typed.trimmed() != drive) return;
    } else if (mode == Export) {
        // Ask where the image goes, every time: the chosen file is what gets written.
        const QString chosen = QFileDialog::getSaveFileName(this, "Where should the image be saved?", absolutize(exportPath_->text()),
                                                            "Images (*.tar.zst *.tar.xz *.tar.gz *.tar);;All files (*)");
        if (chosen.isEmpty()) return;
        exportPath_->setText(chosen);
    } else if (mode == Running) {
        if (QMessageBox::question(this, "Start", "Start now? This changes the system.") != QMessageBox::Yes) return;
    } else if (mode == Iso) {
        const QString name = isoName_->text().trimmed(), tag = isoTag_->text().trimmed();
        if ((!name.isEmpty() && !validWord(name)) || (!tag.isEmpty() && !validWord(tag))) {
            QMessageBox::warning(this, "Create ISO", "The ISO name and tag may only contain letters, digits, '.', '_' and '-'.");
            return;
        }
        // Ask where the ISO goes, every time; it starts in Downloads (created if it does not exist yet).
        QString start = absolutize(isoOut_->text());
        if (start.isEmpty() || !QFileInfo(start).isDir()) start = QFileInfo(userHome() + "/Downloads").isDir() ? userHome() + "/Downloads" : userHome();
        const QString chosen = QFileDialog::getExistingDirectory(this, "Where should the ISO be saved?", start);
        if (chosen.isEmpty()) return;
        isoOut_->setText(chosen);
    }
    runCli(buildArgs(mode), mode, "Starting ...");
}

// Runs the cccu tool with `args` and follows its output. Passwords go through the environment only.
void MainWindow::runCli(const QStringList& args, Mode mode, const QString& title) {
    if (running_) return;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("NO_COLOR", "1");
    if (mode == Install || mode == Running) env.insert("CCCU_USER_PASSWORD", common_[mode].pass->text());
    if (mode == Install) env.insert("CCCU_ROOT_PASSWORD", rootPass_->text());

    log_->clear();
    pending_.clear();
    result_->clear();
    savedIso_.clear();
    bar_->setRange(0, 1);
    bar_->setValue(0);
    stepLabel_->setText(title);
    runningMode_ = mode;
    consoleRun_ = args.contains("--on-console");
    rebootRun_ = args.contains("--on-reboot");

    process_ = new QProcess(this);
    process_->setProcessEnvironment(env);
    process_->setProcessChannelMode(QProcess::MergedChannels);
    connect(process_, &QProcess::readyRead, this, [this] { readOutput(); });
    connect(process_, &QProcess::started, this, [this] { process_->closeWriteChannel(); });   // no prompt can wait for input
    connect(process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) { finished(code, status); });
    connect(process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        appendLog("Could not start " + backendPath() + " - build it with the main CMake target and keep it next to cccu-gui.", "#ff6b6b", true);
        setRunning(false);
    });

    appendLog("$ cccu " + args.join(' '), "#8b93a1");
    setRunning(true);
    process_->start(backendPath(), args);
}

void MainWindow::cancel() {
    if (!process_ || !running_) return;
    const auto answer = QMessageBox::warning(this, "Cancel",
                                             "Stop the running operation?\nCancelling a drive install can leave the drive half-written.",
                                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    process_->terminate();
    QTimer::singleShot(4000, this, [p = QPointer<QProcess>(process_)] {
        if (p && p->state() != QProcess::NotRunning) p->kill();
    });
}

void MainWindow::readOutput() {
    pending_ += process_->readAll();
    pending_.replace('\r', '\n');   // rsync and friends redraw their progress line with carriage returns
    for (qsizetype nl; (nl = pending_.indexOf('\n')) >= 0;) {
        handleLine(QString::fromUtf8(pending_.left(nl)));
        pending_.remove(0, nl + 1);
    }
}

void MainWindow::handleLine(const QString& rawLine) {
    QString line = rawLine;
    line.remove('\r');
    if (line.contains(QChar(0x2588)) || line.contains(QChar(0x2591))) return;   // the text progress bar: the GUI has its own

    // "    ... 42%": progress inside the current step (the bar is 100 units per step).
    static const QRegularExpression progressRe(R"(^\s+\.\.\. (\d+)%(?:\s+(.*))?$)");
    if (const auto p = progressRe.match(line); p.hasMatch()) {
        bar_->setValue(std::min(bar_->maximum(), (currentStep_ - 1) * 100 + p.captured(1).toInt()));
        stepLabel_->setText(stepBase_ + "   " + p.captured(1) + "%   " + p.captured(2));
        return;
    }
    // rsync's own overall progress line: "  1,234,567,890  42%   96.00MB/s    0:00:25"
    static const QRegularExpression rsyncRe(R"(^\s*[\d.,]+\w*\s+(\d+)%\s+(\S+)\s+(\d+:\d+:\d+).*$)");
    if (const auto r = rsyncRe.match(line); r.hasMatch()) {
        bar_->setValue(std::min(bar_->maximum(), (currentStep_ - 1) * 100 + r.captured(1).toInt()));
        stepLabel_->setText(stepBase_ + "   " + r.captured(1) + "%   " + r.captured(2) + "   ETA " + r.captured(3));
        return;
    }
    static const QRegularExpression isoRe(R"(ISO saved: (.+?)\s{2}\()");
    if (const auto m = isoRe.match(line); m.hasMatch()) savedIso_ = m.captured(1);

    static const QRegularExpression stepRe(R"(^\x{2500}{2} \[(\d+)/(\d+)\] (.*?) \x{2500}*$)");
    if (const auto m = stepRe.match(line); m.hasMatch()) {
        const int n = m.captured(1).toInt();
        const int total = m.captured(2).toInt();
        currentStep_ = n;
        bar_->setRange(0, total * 100);
        bar_->setValue((n - 1) * 100);
        stepBase_ = QString("Step %1 of %2  -  %3").arg(n).arg(total).arg(m.captured(3));
        stepLabel_->setText(stepBase_);
        appendLog(line, "#4cd964", true);
    } else if (line.startsWith("[ FAIL ]")) {
        appendLog(line, "#ff6b6b", true);
    } else if (line.startsWith("[ WARN ]")) {
        appendLog(line, "#ffc857");
    } else if (line.startsWith("[  OK  ]")) {
        appendLog(line, "#4cd964");
    } else if (line.startsWith("[ INFO ]")) {
        appendLog(line, "#5fb4ff");
    } else {
        appendLog(line, "#3fa34d");   // what the commands print
    }
}

void MainWindow::finished(int code, QProcess::ExitStatus status) {
    readOutput();   // whatever the process printed last
    if (!pending_.isEmpty()) handleLine(QString::fromUtf8(pending_));
    pending_.clear();
    setRunning(false);
    process_->deleteLater();
    process_ = nullptr;

    bool success = status == QProcess::NormalExit && code == 0;
    // Never trust "exit 0" alone: an export must have produced the file.
    QString savedTo;
    if (success && runningMode_ == Export && !dryRun_->isChecked()) {
        savedTo = absolutize(exportPath_->text());
        if (!QFileInfo(savedTo).isFile() || QFileInfo(savedTo).size() == 0) {
            success = false;
            appendLog("The tool reported success but the image does not exist: " + savedTo, "#ff6b6b", true);
        }
    }
    if (success) bar_->setValue(bar_->maximum());
    stepLabel_->setText(success ? "Finished." : "Stopped.");
    result_->setText(success ? "Completed successfully." : QString("Failed (exit code %1). See the log and /var/log/cccu/.").arg(code));
    result_->setStyleSheet(success ? "color:#4cd964; font-weight:600;" : "color:#ff6b6b; font-weight:600;");

    if (success && !savedTo.isEmpty()) {
        const qint64 size = QFileInfo(savedTo).size();
        result_->setText(QString("Image saved: %1  (%2 MiB)").arg(savedTo).arg(size / (1024 * 1024)));
        QMessageBox::information(this, "Export complete", "The image was saved to:\n\n" + savedTo);
    }
    if (success && runningMode_ == Install) {
        QMessageBox::information(this, "Installation complete",
                                 "Reboot into the new system. On that first boot CCCU creates the desktop container and shows live progress on the screen.");
    }
    if (success && runningMode_ == Iso && !dryRun_->isChecked()) {
        if (rebootRun_) {
            result_->setText("ISO build set up - it runs on the next boot.");
            const auto answer = QMessageBox::question(
                this, "Reboot to build the ISO",
                "The ISO build is set up. After the reboot it runs on the console (VT1) with the progress display, "
                "then the login screen appears.\n\nSave your work. Reboot now?");
            if (answer == QMessageBox::Yes) QProcess::startDetached("systemctl", {"reboot"});
        } else if (consoleRun_) {
            QMessageBox::information(this, "ISO build started",
                                     "The build runs on the console (VT1, Ctrl+Alt+F1) like the first-boot installer.\n"
                                     "Follow it there or with: journalctl -u cccu-iso -f");
        } else if (!savedIso_.isEmpty()) {
            result_->setText("ISO saved: " + savedIso_);
            usbIso_->setText(savedIso_);
            QMessageBox::information(this, "ISO ready", "The ISO was saved to:\n\n" + savedIso_);
        }
    }
    if (runningMode_ == Containers || runningMode_ == Iso) refreshContainers();
}

void MainWindow::setRunning(bool running) {
    running_ = running;
    if (auto* bar = tabs_->findChild<QTabBar*>()) bar->setEnabled(!running);
    for (int i = 0; i < tabs_->count(); ++i) tabs_->widget(i)->setEnabled(!running);
    dryRun_->setEnabled(!running);
    cancelButton_->setEnabled(running);
    refreshStatus();
}

void MainWindow::appendLog(const QString& text, const QString& color, bool bold) {
    log_->appendHtml(QString("<span style=\"color:%1; white-space:pre;%2\">%3</span>")
                         .arg(color, QString(bold ? " font-weight:600;" : ""), text.toHtmlEscaped()));
}
