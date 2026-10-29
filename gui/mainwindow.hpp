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
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QStackedWidget;

// CCCU as a desktop application. It does not duplicate the installer: it collects the settings, runs the
// `cccu` command line tool with them (unattended) and shows its progress and log live.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

private:
    enum Mode { Install = 0, Running = 1, Export = 2 };

    struct Field {
        QLabel* badge;
        bool required;
        std::function<bool()> isSet;
    };
    struct PageData {
        QGridLayout* grid = nullptr;
        int row = 0;
        std::vector<Field> fields;
    };
    // Settings that Install and Running have in common.
    struct Common {
        QLineEdit* user = nullptr;
        QLineEdit* pass = nullptr;
        QLineEdit* containerUser = nullptr;
        QLineEdit* name = nullptr;
        QDoubleSpinBox* size = nullptr;
        QComboBox* compress = nullptr;
        QCheckBox* immutable = nullptr;
    };

    QWidget* buildPage(Mode mode);
    void buildCommon(Mode mode);
    void addRow(Mode mode, const QString& label, QWidget* shown, const QList<QWidget*>& watched, bool required,
                std::function<bool()> isSet);
    QWidget* fileRow(QLineEdit* edit, bool save, const QString& filter);
    void refreshDrives();
    int missing(Mode mode) const;
    void refreshStatus();
    QStringList buildArgs(Mode mode) const;

    void start();
    void cancel();
    void readOutput();
    void handleLine(const QString& line);
    void finished(int code, QProcess::ExitStatus status);
    void setRunning(bool running);
    void appendLog(const QString& text, const QString& color, bool bold = false);
    QString backendPath() const;

    QListWidget* sidebar_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    std::array<PageData, 3> data_;
    std::array<Common, 2> common_;

    // Install
    QComboBox* drive_ = nullptr;
    QLineEdit* installImage_ = nullptr;
    QComboBox* hostSource_ = nullptr;
    QLineEdit* hostImage_ = nullptr;
    QLineEdit* hostname_ = nullptr;
    QLineEdit* rootPass_ = nullptr;
    QComboBox* fs_ = nullptr;
    // Set up on this system
    QComboBox* runSource_ = nullptr;
    QLineEdit* runImage_ = nullptr;
    // Export
    QLineEdit* exportPath_ = nullptr;

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
    Mode runningMode_ = Install;
};
