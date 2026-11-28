#pragma once

#include <QJsonObject>
#include <QObject>
#include <functional>

class QMainWindow;
class QProcess;
class QPushButton;

class UpdateController final : public QObject {
public:
    UpdateController(QMainWindow* owner, std::function<bool()> idle,
                     std::function<void(bool)> setInstalling);

    void start();
    void check(bool manual = true);
    void configure(bool googleShortcut = false);

private:
    void run(bool install, bool manual);
    void showAvailable();
    void clearAvailable();

    QMainWindow* owner_;
    std::function<bool()> idle_;
    std::function<void(bool)> setInstalling_;
    QPushButton* notice_;
    QProcess* process_ = nullptr;
    QJsonObject available_;
    quint64 settingsRevision_ = 0;
    bool installing_ = false;
};
