#include "updater.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMap>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <utility>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
QString userDirectory() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath(
        QCoreApplication::organizationName() + "/" + QCoreApplication::applicationName());
}

QString scriptsDirectory() {
    return QCoreApplication::applicationDirPath() + "/scripts";
}

QJsonObject readObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QJsonObject userConfig(const QJsonObject& installed) {
    const auto path = userDirectory() + "/update_config.json";
    if (QFileInfo::exists(path)) return readObject(path);
    const auto old = readObject(userDirectory() + "/updates.json");
    if (!old.contains("googleSheetCsv")) return {};
    const auto sheet = old.value("googleSheetCsv").toString();
    if (!sheet.isEmpty()) return {{"source", "sheets"}, {"value", sheet}};
    if (!installed.value("githubRepository").toString().isEmpty())
        return {{"source", "github"}, {"value", installed.value("githubRepository")}};
    return {};
}

bool isHttps(const QString& value, const QString& host = {}) {
    const QUrl url(value, QUrl::StrictMode);
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty()
        && url.userInfo().isEmpty() && (host.isEmpty() || url.host() == host);
}

QString configError(const QString& source, const QString& value) {
    if (source == "default") return {};
    if (source == "github") {
        if (QRegularExpression(R"(^[\w.-]+/[\w.-]+$)").match(value).hasMatch()) return {};
        return QStringLiteral("Nhập kho GitHub dạng chủ-sở-hữu/tên-kho.");
    }
    if (source == "sheets") {
        if (isHttps(value, "docs.google.com")) return {};
        return QStringLiteral("Nhập URL CSV HTTPS trên docs.google.com.");
    }
    if (source == "manifest") {
        if (isHttps(value)) return {};
        return QStringLiteral("Nhập URL HTTPS của manifest, không kèm thông tin đăng nhập.");
    }
    const QDir directory(value);
    if (QFileInfo(value).isAbsolute() && directory.exists()
        && QFileInfo(directory.filePath("Setup.exe")).isFile()
        && QFileInfo(directory.filePath("update-feed.json")).isFile()) return {};
    return QStringLiteral("Chọn thư mục có Setup.exe và update-feed.json.");
}
}

UpdateController::UpdateController(QMainWindow* owner, std::function<bool()> idle,
                                   std::function<void(bool)> setInstalling)
    : QObject(owner), owner_(owner), idle_(std::move(idle)),
      setInstalling_(std::move(setInstalling)), notice_(new QPushButton(owner)) {
    notice_->setObjectName("updateNotice");
    notice_->setToolTip(QStringLiteral("Xem nội dung cập nhật"));
    owner_->statusBar()->addPermanentWidget(notice_);
    notice_->hide();
    connect(notice_, &QPushButton::clicked, this, &UpdateController::showAvailable);
}

void UpdateController::start() {
#ifdef Q_OS_WIN
    if (QFileInfo(scriptsDirectory() + "/version.txt").isFile()) {
        QTimer::singleShot(2000, this, [this] {
            const auto installed = readObject(scriptsDirectory() + "/update-feed.json");
            if (userConfig(installed).value("autoCheck").toBool(true)) check(false);
        });
    }
#endif
}

void UpdateController::check(bool manual) {
    if (process_) {
        if (manual)
            owner_->statusBar()->showMessage(installing_
                ? QStringLiteral("Đang chuẩn bị cài bản mới…")
                : QStringLiteral("Đang kiểm tra bản mới…"), 4000);
        return;
    }
    if (manual && !available_.isEmpty()) {
        showAvailable();
        return;
    }
    run(false, manual);
}

void UpdateController::clearAvailable() {
    available_ = {};
    notice_->hide();
}

void UpdateController::configure(bool googleShortcut) {
    if (installing_) return;
    const auto installed = readObject(scriptsDirectory() + "/update-feed.json");
    const auto config = userConfig(installed);
    QDialog dialog(owner_);
    dialog.setWindowTitle(QStringLiteral("Cấu hình Cập nhật"));
    dialog.setMinimumWidth(505);
    QFormLayout layout(&dialog);
    QComboBox source;
    source.setObjectName("updateSource");
    source.addItem(QStringLiteral("Mặc định của bản cài"), "default");
    source.addItem(QStringLiteral("GitHub Releases"), "github");
    source.addItem(QStringLiteral("Google Sheets (CSV công khai)"), "sheets");
    source.addItem(QStringLiteral("Manifest JSON qua HTTPS"), "manifest");
    source.addItem(QStringLiteral("Thư mục gói cập nhật"), "local");
    const auto oldSource = config.value("source").toString("default");
    source.setCurrentIndex(qMax(0, source.findData(googleShortcut ? QString("sheets") : oldSource)));

    QWidget location;
    QHBoxLayout locationLayout(&location);
    locationLayout.setContentsMargins(0,0,0,0);
    QLineEdit value;
    value.setObjectName("updateSourceValue");
    if (source.currentData().toString() == oldSource) value.setText(config.value("value").toString());
    if (value.text().isEmpty() && source.currentData().toString() == "sheets")
        value.setText(installed.value("googleSheetCsv").toString());
    QPushButton browse(QStringLiteral("Chọn thư mục…"));
    locationLayout.addWidget(&value, 1);
    locationLayout.addWidget(&browse);
    QCheckBox autoCheck(QStringLiteral("Tự kiểm tra bản mới khi mở ArtisanAxis"));
    autoCheck.setObjectName("updateAutoCheck");
    autoCheck.setChecked(config.value("autoCheck").toBool(true));
    QCheckBox keepBackup(QStringLiteral("Giữ bản sao sau khi cập nhật thành công"));
    keepBackup.setObjectName("updateKeepBackup");
    keepBackup.setChecked(config.value("keepBackup").toBool(false));

    QLabel policy(installed.value("publisherSignerSha1").toString().trimmed().isEmpty()
        ? QStringLiteral("Bộ cài được đối chiếu phiên bản và SHA-256. Chọn nguồn của xưởng.")
        : QStringLiteral("Bộ cài phải khớp phiên bản, SHA-256 và chứng chỉ đã ghim."));
    policy.setWordWrap(true);
    QLabel rollback(QStringLiteral("ArtisanAxis luôn sao lưu để có thể khôi phục nếu bản mới không khởi động."));
    rollback.setWordWrap(true);
    const auto defaultDescription = installed.value("googleSheetCsv").toString().isEmpty()
        ? installed.value("githubRepository").toString() : installed.value("googleSheetCsv").toString();
    QLabel defaultSource(defaultDescription.isEmpty() ? QStringLiteral("Chưa thiết lập") : defaultDescription);
    defaultSource.setWordWrap(true);
    defaultSource.setTextFormat(Qt::PlainText);
    QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);

    layout.addRow(QStringLiteral("Nguồn cập nhật"), &source);
    layout.addRow(QStringLiteral("Nguồn của bản cài"), &defaultSource);
    layout.addRow(QStringLiteral("Địa chỉ"), &location);
    layout.addRow(&autoCheck);
    layout.addRow(&keepBackup);
    layout.addRow(&policy);
    layout.addRow(&rollback);
    layout.addRow(&buttons);

    QMap<QString,QString> drafts;
    drafts[oldSource] = config.value("value").toString();
    auto displayedSource = source.currentData().toString();
    auto setFieldState = [&] {
        const auto type = source.currentData().toString();
        value.setEnabled(type != "default");
        browse.setVisible(type == "local");
        if (type == "github") value.setPlaceholderText(QStringLiteral("chủ-sở-hữu/tên-kho"));
        else if (type == "sheets") value.setPlaceholderText(QStringLiteral("https://docs.google.com/…"));
        else if (type == "manifest") value.setPlaceholderText(QStringLiteral("https://…/update-feed.json"));
        else if (type == "local") value.setPlaceholderText(QStringLiteral("Thư mục chứa gói cập nhật"));
        else value.setPlaceholderText({});
    };
    setFieldState();
    connect(&source, &QComboBox::currentIndexChanged, &dialog, [&] {
        drafts[displayedSource] = value.text();
        displayedSource = source.currentData().toString();
        const auto installedDefault = displayedSource == "github" ? installed.value("githubRepository").toString()
            : displayedSource == "sheets" ? installed.value("googleSheetCsv").toString() : QString{};
        value.setText(drafts.value(displayedSource, installedDefault));
        setFieldState();
    });
    connect(&browse, &QPushButton::clicked, &dialog, [&] {
        const auto chosen = QFileDialog::getExistingDirectory(&dialog, QStringLiteral("Chọn thư mục gói cập nhật"), value.text());
        if (!chosen.isEmpty()) value.setText(QDir::toNativeSeparators(chosen));
    });
    connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const auto type = source.currentData().toString();
        const auto address = type == "default" ? QString{} : value.text().trimmed();
        if (const auto error = configError(type, address); !error.isEmpty()) {
            QMessageBox::warning(&dialog, QStringLiteral("Cấu hình Cập nhật"), error);
            return;
        }
        const QJsonObject saved{{"source", type}, {"value", address},
            {"autoCheck", autoCheck.isChecked()}, {"keepBackup", keepBackup.isChecked()}};
        const auto bytes = QJsonDocument(saved).toJson();
        QDir().mkpath(userDirectory());
        QSaveFile file(userDirectory() + "/update_config.json");
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
            QMessageBox::warning(&dialog, QStringLiteral("Cấu hình Cập nhật"), file.errorString());
            return;
        }
        ++settingsRevision_;
        clearAvailable();
        dialog.accept();
    });
    dialog.exec();
}

void UpdateController::showAvailable() {
    if (available_.isEmpty() || installing_) return;
    QDialog dialog(owner_);
    dialog.setWindowTitle(QStringLiteral("Bản cập nhật ArtisanAxis"));
    dialog.resize(520, 345);
    QVBoxLayout layout(&dialog);
    QLabel version(QStringLiteral("Đã có ArtisanAxis %1").arg(available_.value("version").toString()));
    version.setTextFormat(Qt::PlainText);
    layout.addWidget(&version);
    QPlainTextEdit notes;
    notes.setObjectName("updateReleaseNotes");
    notes.setReadOnly(true);
    const auto text = available_.value("notes").toString();
    notes.setPlainText(text.isEmpty() ? QStringLiteral("Bản phát hành chưa kèm ghi chú.") : text);
    layout.addWidget(&notes);
    QDialogButtonBox actions(QDialogButtonBox::Cancel);
    actions.button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Để sau"));
    auto* apply = actions.addButton(QStringLiteral("Sao lưu và cài bản mới"), QDialogButtonBox::AcceptRole);
    QLabel busy(QStringLiteral("Hoàn tất tác vụ hiện tại trước khi cài đặt."));
    busy.setWordWrap(true);
    const auto canInstall = idle_();
    busy.setVisible(!canInstall);
    apply->setEnabled(canInstall);
    layout.addWidget(&busy);
    layout.addWidget(&actions);
    connect(&actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(&actions, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    if (dialog.exec() == QDialog::Accepted && idle_()) run(true, true);
}

void UpdateController::run(bool install, bool manual) {
#ifdef Q_OS_WIN
    if (process_ || (install && (!idle_() || available_.isEmpty()))) return;
    auto* process = new QProcess(this);
    process_ = process;
    installing_ = install;
    if (install) setInstalling_(true);
    if (manual)
        owner_->statusBar()->showMessage(install ? QStringLiteral("Đang chuẩn bị cài bản mới…")
                                                : QStringLiteral("Đang kiểm tra bản mới…"));
    const auto revision = settingsRevision_;
    auto finish = [this, process, revision, install, manual](QString error) {
        if (process_ != process) return;
        const auto result = QJsonDocument::fromJson(process->readAllStandardOutput()).object();
        process_ = nullptr;
        process->deleteLater();
        installing_ = false;
        if (install) setInstalling_(false);
        if (revision != settingsRevision_) return;
        if (result.value("ok").toBool() && error.isEmpty()) {
            if (install && result.value("ready").toBool()) {
                owner_->close();
                return;
            }
            if (!install && result.value("available").toBool()) {
                available_ = result;
                notice_->setText(QStringLiteral("Có bản mới %1").arg(result.value("version").toString()));
                notice_->show();
                if (manual) showAvailable();
                return;
            }
            if (!result.value("available").toBool()) {
                clearAvailable();
                if (manual) owner_->statusBar()->showMessage(QStringLiteral("Ứng dụng đã ở phiên bản mới nhất."), 6000);
                return;
            }
        }
        if (!manual) return;
        if (install) clearAvailable();
        if (error.isEmpty())
            error = result.value("error").toString(QStringLiteral("Trình cập nhật không trả kết quả hợp lệ."));
        owner_->statusBar()->clearMessage();
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Cập nhật 1-Click"), error, QMessageBox::Ok, owner_);
        box.setTextFormat(Qt::PlainText);
        box.exec();
    };
    connect(process, &QProcess::finished, this, [finish](int, QProcess::ExitStatus exit) {
        finish(exit == QProcess::CrashExit ? QStringLiteral("Trình cập nhật đã dừng ngoài dự kiến.") : QString{});
    });
    connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(QStringLiteral("Chưa mở được Windows PowerShell."));
    });
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    QTimer::singleShot(install ? 450000 : 100000, process, [process] {
        if (process->state() != QProcess::NotRunning) process->kill();
    });
    QStringList args{"-NoLogo", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                     "-File", scriptsDirectory() + "/Update.ps1"};
    if (install) args << "-Apply" << "-ParentPid" << QString::number(QCoreApplication::applicationPid())
                      << "-ExpectedVersion" << available_.value("version").toString()
                      << "-ExpectedSha256" << available_.value("sha256").toString();
    const auto system = qEnvironmentVariable("SystemRoot", "C:/Windows");
    process->start(QDir(system).filePath("System32/WindowsPowerShell/v1.0/powershell.exe"), args);
#else
    Q_UNUSED(install);
    if (manual) QMessageBox::information(owner_, QStringLiteral("Cập nhật"),
                                         QStringLiteral("Bộ cập nhật cài đặt dành cho gói Windows."));
#endif
}
