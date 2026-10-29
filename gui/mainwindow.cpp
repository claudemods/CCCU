#include "mainwindow.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#include <unistd.h>

namespace {

const char* kStyle = R"(
* { font-size: 13px; }
QMainWindow, QWidget#main { background: #1b1e24; }
QWidget { color: #e6e8eb; }
QScrollArea, QStackedWidget { background: transparent; border: none; }
QWidget#side { background: #14161b; }
QLabel#brand { font-size: 30px; font-weight: 700; color: #ff5a5f; letter-spacing: 3px; }
QLabel#muted { color: #8b93a1; }
QLabel#h1 { font-size: 20px; font-weight: 600; }
QListWidget { background: transparent; border: none; outline: none; }
QListWidget::item { padding: 11px 12px; border-radius: 6px; margin: 2px 0; }
QListWidget::item:selected { background: #2a3140; color: #ffffff; }
QListWidget::item:hover:!selected { background: #1f242d; }
QLineEdit, QComboBox, QDoubleSpinBox { background: #12151a; border: 1px solid #343b49; border-radius: 5px; padding: 6px 8px; min-height: 18px; }
QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus { border: 1px solid #3b82f6; }
QComboBox QAbstractItemView { background: #12151a; selection-background-color: #2a3140; }
QPushButton { background: #2a3140; border: 1px solid #3a4254; border-radius: 5px; padding: 7px 16px; }
QPushButton:hover { background: #343c4e; }
QPushButton:disabled { color: #6b7280; background: #20242c; }
QPushButton#start { background: #1e8e3e; border: none; font-weight: 600; padding: 9px 28px; }
QPushButton#start:hover { background: #23a347; }
QPushButton#start:disabled { background: #274533; color: #7d9a86; }
QPushButton#cancel { background: #8c2a2a; border: none; }
QLabel[state="set"] { background: #1e8e3e; color: white; border-radius: 4px; padding: 3px 0; font-weight: 700; font-size: 11px; }
QLabel[state="missing"] { background: #c62828; color: white; border-radius: 4px; padding: 3px 0; font-weight: 700; font-size: 11px; }
QLabel[state="optional"] { background: #3a4254; color: #b6bdca; border-radius: 4px; padding: 3px 0; font-weight: 700; font-size: 11px; }
QLabel#summary[state="ok"] { background: #1e8e3e; color: white; border-radius: 4px; padding: 6px 12px; font-weight: 600; }
QLabel#summary[state="bad"] { background: #c62828; color: white; border-radius: 4px; padding: 6px 12px; font-weight: 600; }
QLabel#warning { background: #7a2e12; color: #ffd9c7; border-radius: 4px; padding: 8px 12px; }
QProgressBar { background: #12151a; border: 1px solid #343b49; border-radius: 5px; text-align: center; min-height: 18px; }
QProgressBar::chunk { background: #1e8e3e; border-radius: 4px; }
QPlainTextEdit { background: #0e1014; border: 1px solid #2a303c; border-radius: 6px; padding: 6px; }
QCheckBox::indicator { width: 16px; height: 16px; }
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
    setWindowTitle("CCCU - Claudemods Container Creator Unity");
    resize(1160, 820);
    setStyleSheet(kStyle);

    auto* central = new QWidget;
    setCentralWidget(central);
    auto* outer = new QHBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // ---- sidebar ----
    auto* side = new QWidget;
    side->setObjectName("side");
    side->setFixedWidth(250);
    auto* sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(18, 22, 18, 18);
    sideLayout->addWidget(makeLabel("CCCU", "brand"));
    sideLayout->addWidget(makeLabel("Claudemods Container\nCreator Unity", "muted"));
    sideLayout->addSpacing(18);
    sidebar_ = new QListWidget;
    sidebar_->addItems({"Install to a drive", "Set up on this system", "Export this machine"});
    sideLayout->addWidget(sidebar_);
    sideLayout->addStretch();
    sideLayout->addWidget(makeLabel(QString("Version %1").arg(CCCU_GUI_VERSION), "muted"));
    outer->addWidget(side);

    // ---- main area ----
    auto* main = new QWidget;
    main->setObjectName("main");
    auto* col = new QVBoxLayout(main);
    col->setContentsMargins(26, 22, 26, 18);
    col->setSpacing(12);

    rootWarning_ = makeLabel("CCCU needs administrator rights to change the system, and they could not be requested "
                             "(is polkit / pkexec installed?). Close this window and open CCCU again.", "warning");
    rootWarning_->setWordWrap(true);
    rootWarning_->setVisible(::geteuid() != 0);
    col->addWidget(rootWarning_);

    pages_ = new QStackedWidget;
    for (Mode mode : {Install, Running, Export}) pages_->addWidget(buildPage(mode));
    col->addWidget(pages_, 3);

    // status + buttons
    auto* actions = new QHBoxLayout;
    summary_ = makeLabel("", "summary");
    dryRun_ = new QCheckBox("Dry run (change nothing)");
    startButton_ = new QPushButton("Start");
    startButton_->setObjectName("start");
    cancelButton_ = new QPushButton("Cancel");
    cancelButton_->setObjectName("cancel");
    cancelButton_->setEnabled(false);
    auto* openLog = new QPushButton("Open log folder");
    actions->addWidget(summary_);
    actions->addStretch();
    actions->addWidget(dryRun_);
    actions->addWidget(openLog);
    actions->addWidget(cancelButton_);
    actions->addWidget(startButton_);
    col->addLayout(actions);

    // progress + log
    stepLabel_ = makeLabel("Ready.", "muted");
    bar_ = new QProgressBar;
    bar_->setRange(0, 1);
    bar_->setValue(0);
    bar_->setFormat("%p%");
    result_ = new QLabel;
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(20000);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    col->addWidget(stepLabel_);
    col->addWidget(bar_);
    col->addWidget(result_);
    col->addWidget(log_, 2);
    outer->addWidget(main, 1);

    connect(sidebar_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) pages_->setCurrentIndex(row);
        refreshStatus();
    });
    connect(startButton_, &QPushButton::clicked, this, [this] { start(); });
    connect(cancelButton_, &QPushButton::clicked, this, [this] { cancel(); });
    connect(openLog, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(QUrl::fromLocalFile("/var/log/cccu")); });

    sidebar_->setCurrentRow(0);
    refreshStatus();
}

// ---- pages ------------------------------------------------------------------------------------

QWidget* MainWindow::buildPage(Mode mode) {
    static const char* titles[] = {"Install to a drive", "Set up on this system", "Export this machine"};
    static const char* texts[] = {
        "Wipes the drive: EFI boot partition, tty host system with Incus, and your desktop container (created on the first boot).",
        "Installs what this machine needs (Incus, SDDM, cage ...) with its own package manager, then creates the container. Reboot afterwards.",
        "Writes this running system as a desktop image that can be cloned on another machine."};

    auto* page = new QWidget;
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 8, 0);
    v->addWidget(makeLabel(titles[mode], "h1"));
    auto* text = makeLabel(texts[mode], "muted");
    text->setWordWrap(true);
    v->addWidget(text);

    auto* host = new QWidget;
    auto* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 10, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(9);
    grid->setColumnStretch(1, 1);
    data_[mode].grid = grid;
    v->addWidget(host);
    v->addStretch();

    const QString imageFilter = "System images (*.tar *.tar.zst *.tar.xz *.tar.gz *.squashfs *.sfs *.erofs);;All files (*)";
    const auto isFile = [](QLineEdit* edit) { return [edit] { return QFileInfo(edit->text().trimmed()).isFile(); }; };

    if (mode == Install) {
        drive_ = new QComboBox;
        auto* driveRow = new QWidget;
        auto* driveLayout = new QHBoxLayout(driveRow);
        driveLayout->setContentsMargins(0, 0, 0, 0);
        auto* refresh = new QPushButton("Refresh");
        driveLayout->addWidget(drive_, 1);
        driveLayout->addWidget(refresh);
        connect(refresh, &QPushButton::clicked, this, [this] { refreshDrives(); });
        addRow(Install, "Target drive (WIPED)", driveRow, {drive_}, true, [this] { return !drive_->currentData().toString().isEmpty(); });
        refreshDrives();

        installImage_ = new QLineEdit;
        installImage_->setPlaceholderText("Desktop image made with --export");
        addRow(Install, "Desktop image", fileRow(installImage_, false, imageFilter), {installImage_}, true, isFile(installImage_));

        hostSource_ = new QComboBox;
        hostSource_->addItems({"Live ISO image (default)", "Clone of this running system", "Image file ..."});
        hostImage_ = new QLineEdit;
        hostImage_->setPlaceholderText("tty host system image");
        auto* hostRow = new QWidget;
        auto* hostLayout = new QHBoxLayout(hostRow);
        hostLayout->setContentsMargins(0, 0, 0, 0);
        hostLayout->addWidget(hostSource_);
        hostLayout->addWidget(fileRow(hostImage_, false, imageFilter), 1);
        hostImage_->parentWidget()->setEnabled(false);
        connect(hostSource_, &QComboBox::currentIndexChanged, this, [this](int index) { hostImage_->parentWidget()->setEnabled(index == 2); });
        addRow(Install, "tty host system", hostRow, {hostSource_, hostImage_}, true,
               [this, f = isFile(hostImage_)] { return hostSource_->currentIndex() < 2 || f(); });

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
        sourceLayout->addWidget(runSource_);
        sourceLayout->addWidget(fileRow(runImage_, false, imageFilter), 1);
        connect(runSource_, &QComboBox::currentIndexChanged, this, [this](int index) { runImage_->parentWidget()->setEnabled(index == 0); });
        addRow(Running, "System to clone", sourceRow, {runSource_, runImage_}, true,
               [this, f = isFile(runImage_)] { return runSource_->currentIndex() == 1 || f(); });
        buildCommon(Running);
    } else {
        exportPath_ = new QLineEdit("desktop.tar.zst");
        addRow(Export, "Output file", fileRow(exportPath_, true, "Images (*.tar *.tar.zst *.tar.xz *.tar.gz)"), {exportPath_}, true,
               [this] { return !exportPath_->text().trimmed().isEmpty(); });
    }

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(page);
    return scroll;
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
        rootPass_->setPlaceholderText("Root password of the new host (tty login)");
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

    c.size = new QDoubleSpinBox;
    c.size->setDecimals(0);
    c.size->setRange(0, 100000);
    c.size->setSuffix(" GiB");
    c.size->setSpecialValueText("not set");
    addRow(mode, "Container size", c.size, {c.size}, true, [s = c.size] { return s->value() >= 1; });

    c.compress = new QComboBox;
    c.compress->addItems({"zstd:22", "zstd:15", "zstd:9", "zstd:3", "zlib:9", "lzo", "none"});
    addRow(mode, "Compression", c.compress, {c.compress}, true, [] { return true; });

    c.immutable = new QCheckBox("Mount the container system read-only");
    addRow(mode, "Immutable container", c.immutable, {c.immutable}, false, [i = c.immutable] { return i->isChecked(); });
}

void MainWindow::addRow(Mode mode, const QString& label, QWidget* shown, const QList<QWidget*>& watched, bool required,
                        std::function<bool()> isSet) {
    PageData& page = data_[mode];
    auto* badge = new QLabel;
    badge->setFixedWidth(84);
    badge->setAlignment(Qt::AlignCenter);
    page.grid->addWidget(new QLabel(label), page.row, 0);
    page.grid->addWidget(shown, page.row, 1);
    page.grid->addWidget(badge, page.row, 2);
    ++page.row;
    page.fields.push_back({badge, required, std::move(isSet)});

    for (QWidget* w : watched) {
        if (auto* e = qobject_cast<QLineEdit*>(w)) connect(e, &QLineEdit::textChanged, this, [this] { refreshStatus(); });
        else if (auto* c = qobject_cast<QComboBox*>(w)) connect(c, &QComboBox::currentIndexChanged, this, [this] { refreshStatus(); });
        else if (auto* b = qobject_cast<QCheckBox*>(w)) connect(b, &QCheckBox::toggled, this, [this] { refreshStatus(); });
        else if (auto* s = qobject_cast<QDoubleSpinBox*>(w)) connect(s, &QDoubleSpinBox::valueChanged, this, [this] { refreshStatus(); });
    }
}

QWidget* MainWindow::fileRow(QLineEdit* edit, bool save, const QString& filter) {
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* browse = new QPushButton("Browse ...");
    layout->addWidget(edit, 1);
    layout->addWidget(browse);
    connect(browse, &QPushButton::clicked, this, [this, edit, save, filter] {
        const QString path = save ? QFileDialog::getSaveFileName(this, "Choose the output file", edit->text(), filter)
                                  : QFileDialog::getOpenFileName(this, "Choose a file", edit->text(), filter);
        if (!path.isEmpty()) edit->setText(path);
    });
    return row;
}

void MainWindow::refreshDrives() {
    drive_->clear();
    QProcess lsblk;
    lsblk.start("lsblk", {"-dnpr", "-e", "1,7,11", "-o", "NAME,SIZE,MODEL"});
    if (!lsblk.waitForFinished(4000)) return;
    const QStringList lines = QString::fromUtf8(lsblk.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        const QStringList parts = line.split(' ');
        if (parts.size() < 2 || parts[0].contains("zram")) continue;
        QString model = parts.mid(2).join(' ');
        model.replace("\\x20", " ");
        drive_->addItem(QString("%1   %2   %3").arg(parts[0], parts[1], model.trimmed()), parts[0]);
    }
    drive_->setCurrentIndex(-1);
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
    if (!startButton_ || !summary_) return;   // the window is still being built
    for (const PageData& page : data_) {
        for (const Field& f : page.fields) {
            if (f.isSet()) setBadge(f.badge, "set", "SET");
            else if (f.required) setBadge(f.badge, "missing", "NOT SET");
            else setBadge(f.badge, "optional", "OPTIONAL");
        }
    }
    const Mode mode = static_cast<Mode>(std::max(0, pages_->currentIndex()));
    const int n = missing(mode);
    summary_->setText(n == 0 ? "READY  -  everything is set" : QString("%1 required setting%2 not set").arg(n).arg(n == 1 ? "" : "s"));
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
        args << "--export" << exportPath_->text().trimmed();
    } else {
        const Common& c = common_[mode];
        const QString user = c.user->text().trimmed();
        const QString containerUser = c.containerUser->text().trimmed().isEmpty() ? user : c.containerUser->text().trimmed();
        if (mode == Install) {
            args << "install" << "--drive" << drive_->currentData().toString() << "--image" << installImage_->text().trimmed()
                 << "--hostname" << hostname_->text().trimmed() << "--fs" << fs_->currentText();
            if (hostSource_->currentIndex() == 1) args << "--clone-system";
            if (hostSource_->currentIndex() == 2) args << "--host-image" << hostImage_->text().trimmed();
        } else {
            args << "setup";
            if (runSource_->currentIndex() == 1) args << "--clone-host";
            else args << "--image" << runImage_->text().trimmed();
        }
        args << "--user" << user << "--container-user" << containerUser << "--name" << c.name->text().trimmed() << "--size"
             << QString("%1GiB").arg(static_cast<long long>(c.size->value())) << "--compress" << c.compress->currentText();
        if (c.immutable->isChecked()) args << "--immutable";
    }
    if (dryRun_->isChecked()) args << "--dry-run";
    args << "--yes";
    return args;
}

void MainWindow::start() {
    const Mode mode = static_cast<Mode>(pages_->currentIndex());
    if (missing(mode) != 0 || running_) return;

    if (mode == Install) {
        const QString drive = drive_->currentData().toString();
        bool accepted = false;
        const QString typed = QInputDialog::getText(this, "Confirm the wipe",
                                                    QString("ALL DATA on %1 will be destroyed.\n\nType the device path to confirm:").arg(drive),
                                                    QLineEdit::Normal, QString(), &accepted);
        if (!accepted || typed.trimmed() != drive) return;
    } else if (QMessageBox::question(this, "Start", "Start now? This changes the system.") != QMessageBox::Yes) {
        return;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("NO_COLOR", "1");
    if (mode != Export) env.insert("CCCU_USER_PASSWORD", common_[mode].pass->text());
    if (mode == Install) env.insert("CCCU_ROOT_PASSWORD", rootPass_->text());

    log_->clear();
    pending_.clear();
    result_->clear();
    bar_->setRange(0, 1);
    bar_->setValue(0);
    stepLabel_->setText("Starting ...");
    runningMode_ = mode;

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

    const QStringList args = buildArgs(mode);
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
    for (qsizetype nl; (nl = pending_.indexOf('\n')) >= 0;) {
        handleLine(QString::fromUtf8(pending_.left(nl)));
        pending_.remove(0, nl + 1);
    }
}

void MainWindow::handleLine(const QString& rawLine) {
    QString line = rawLine;
    line.remove('\r');
    if (line.contains(QChar(0x2588)) || line.contains(QChar(0x2591))) return;   // the text progress bar: the GUI has its own

    static const QRegularExpression stepRe(R"(^\x{2500}{2} \[(\d+)/(\d+)\] (.*?) \x{2500}*$)");
    if (const auto m = stepRe.match(line); m.hasMatch()) {
        const int n = m.captured(1).toInt();
        const int total = m.captured(2).toInt();
        bar_->setRange(0, total);
        bar_->setValue(n - 1);
        stepLabel_->setText(QString("Step %1 of %2  -  %3").arg(n).arg(total).arg(m.captured(3)));
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
    if (!pending_.isEmpty()) handleLine(QString::fromUtf8(pending_));
    pending_.clear();
    setRunning(false);
    process_->deleteLater();
    process_ = nullptr;

    const bool success = status == QProcess::NormalExit && code == 0;
    if (success) bar_->setValue(bar_->maximum());
    stepLabel_->setText(success ? "Finished." : "Stopped.");
    result_->setText(success ? "Completed successfully." : QString("Failed (exit code %1). See the log above and /var/log/cccu/.").arg(code));
    result_->setStyleSheet(success ? "color:#4cd964; font-weight:600;" : "color:#ff6b6b; font-weight:600;");

    if (success && runningMode_ == Install) {
        QMessageBox::information(this, "Installation complete",
                                 "Reboot into the new system. On that first boot CCCU creates the desktop container and shows live progress on the screen.");
    }
}

void MainWindow::setRunning(bool running) {
    running_ = running;
    sidebar_->setEnabled(!running);
    pages_->setEnabled(!running);
    dryRun_->setEnabled(!running);
    cancelButton_->setEnabled(running);
    refreshStatus();
}

void MainWindow::appendLog(const QString& text, const QString& color, bool bold) {
    log_->appendHtml(QString("<span style=\"color:%1; white-space:pre;%2\">%3</span>")
                         .arg(color, bold ? " font-weight:600;" : "", text.toHtmlEscaped()));
}
