#pragma once
#include <QByteArray>
#include <QList>
#include <QMainWindow>
#include <QProcess>

#include <array>
#include <functional>
#include <vector>

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;

// CCCU as a desktop application. It does not duplicate the tool: it collects the settings, runs the `cccu`
// command line tool with them (unattended) and shows its progress and log live. Everything fits on one
// screen: tabs on top, the settings of the tab in the middle, progress, log and buttons at the bottom.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

private:
    enum Mode { Install = 0, Running = 1, Export = 2, Iso = 3, Containers = 4 };
    static constexpr int kModes = 5;

    struct Field {
        QLabel* badge;
        bool required;
        std::function<bool()> isSet;
    };
    struct PageData {
        QGridLayout* grid = nullptr;
        int cell = 0;                  // next free cell of the two-column grid
        std::vector<Field> fields;
    };
    // Settings that Install and Running have in common.
    struct Common {
        QLineEdit* user = nullptr;
        QLineEdit* pass = nullptr;
        QLineEdit* containerUser = nullptr;
        QLineEdit* name = nullptr;
        QSpinBox* size = nullptr;
        QComboBox* compress = nullptr;
        QCheckBox* immutable = nullptr;
    };

    QWidget* buildPage(Mode mode);
    QWidget* buildIsoPage();
    QWidget* buildContainersPage();
    QWidget* card(const QString& title, const QString& text, QWidget** formHost, Mode mode);
    void buildCommon(Mode mode);
    void addRow(Mode mode, const QString& label, QWidget* shown, const QList<QWidget*>& watched, bool required,
                std::function<bool()> isSet, bool wide = false);
    QWidget* fileRow(QLineEdit* edit, bool save, const QString& filter, bool folder = false);
    void refreshDrives(QComboBox* box);
    void refreshContainers();
    int missing(Mode mode) const;
    void refreshStatus();
    QStringList buildArgs(Mode mode) const;
    QString selectedContainer() const;

    void start();
    void runCli(const QStringList& args, Mode mode, const QString& title);
    void cancel();
    void readOutput();
    void handleLine(const QString& line);
    void finished(int code, QProcess::ExitStatus status);
    void setRunning(bool running);
    void appendLog(const QString& text, const QString& color, bool bold = false);
    QString backendPath() const;
    Mode currentMode() const;

    QTabWidget* tabs_ = nullptr;
    std::array<PageData, kModes> data_;
    std::array<Common, 2> common_;

    // Install
    QComboBox* drive_ = nullptr;
    QLineEdit* installImage_ = nullptr;
    QComboBox* hostSource_ = nullptr;
    QLineEdit* hostImage_ = nullptr;
    QComboBox* hostPreset_ = nullptr;
    QLineEdit* hostname_ = nullptr;
    QLineEdit* rootPass_ = nullptr;
    QComboBox* fs_ = nullptr;
    // Set up on this system
    QComboBox* runSource_ = nullptr;
    QLineEdit* runImage_ = nullptr;
    // Export
    QLineEdit* exportPath_ = nullptr;
    // ISO
    QComboBox* isoContainer_ = nullptr;
    QLineEdit* isoOut_ = nullptr;
    QLineEdit* isoName_ = nullptr;
    QLineEdit* isoTag_ = nullptr;
    QComboBox* isoFormat_ = nullptr;
    QSpinBox* isoLevel_ = nullptr;
    QComboBox* isoInstaller_ = nullptr;
    QCheckBox* isoRam_ = nullptr;
    QCheckBox* isoCache_ = nullptr;
    QComboBox* isoWhen_ = nullptr;
    QLineEdit* usbIso_ = nullptr;
    QComboBox* usbDrive_ = nullptr;
    // Containers
    QTableWidget* table_ = nullptr;
    QLineEdit* copyName_ = nullptr;
    QCheckBox* copySwitch_ = nullptr;
    QLabel* containerHint_ = nullptr;

    QLabel* rootWarning_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* stepLabel_ = nullptr;
    QLabel* result_ = nullptr;
    QProgressBar* bar_ = nullptr;
    QPlainTextEdit* log_ = nullptr;
    QCheckBox* dryRun_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;

    QProcess* process_ = nullptr;
    QByteArray pending_;     // output not yet ended by a newline
    bool running_ = false;
    int currentStep_ = 1;
    QString stepBase_;       // "Step 3 of 12  -  Title": the detail (bytes, speed, ETA) is appended to it
    Mode runningMode_ = Install;
    QString savedIso_;       // "ISO saved: <path>" seen in the output
    bool consoleRun_ = false;
    bool rebootRun_ = false;
};
