#define main artisan_axis_application_main
#include "../main.cpp"
#undef main

#include <cstdlib>
#include <iostream>

namespace {
void check(bool result, const char* description) {
    if (!result) {
        std::cerr << "FAIL " << description << '\n';
        std::exit(EXIT_FAILURE);
    }
    std::cout << "PASS " << description << '\n';
}

QDialog* modal() {
    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    check(dialog, "modal dialog opens");
    return dialog;
}

template <typename Widget>
Widget* field(QDialog* dialog, const char* label) {
    for (auto* layout : dialog->findChildren<QFormLayout*>()) {
        for (int row = 0; row < layout->rowCount(); ++row) {
            auto* labelItem = layout->itemAt(row, QFormLayout::LabelRole);
            auto* fieldItem = layout->itemAt(row, QFormLayout::FieldRole);
            if (!labelItem || !fieldItem) continue;
            auto* caption = qobject_cast<QLabel*>(labelItem->widget());
            if (caption && caption->text() == QString::fromUtf8(label)) {
                auto* control = qobject_cast<Widget*>(fieldItem->widget());
                check(control, label);
                return control;
            }
        }
    }
    check(false, label);
    return nullptr;
}

void accept(QDialog* dialog) {
    auto* buttons = dialog->findChild<QDialogButtonBox*>();
    check(buttons, "dialog has commit button box");
    auto* okay = buttons->button(QDialogButtonBox::Ok);
    check(okay && okay->isEnabled(), "dialog commit enabled");
    okay->click();
}

QAction* action(Workshop& workshop, const QString& label) {
    for (auto* item : workshop.findChildren<QAction*>())
        if (item->text() == label) return item;
    check(false, qPrintable(label));
    return nullptr;
}
}

struct WorkshopAudit {
    static void checkMode(Workshop& workshop,int axes,const QString& post) {
        const auto plan=workshop.flow.snapshot();
        check(workshop.modeBox->currentData().toInt()==axes && plan["axes"].toInt()==axes &&
              plan["post"].toString()==post, "toolbar, CAM plan and post agree");
        for(auto item:plan["paths"].toArray()){
            const auto path=item.toObject();
            check(path["axes"].toInt()==(path["faceAngle"].toInt()<0?axes:4),
                  "rotary passes follow mode and raster remains 4X");
        }
    }
    static void checkWorkerLock(Workshop& workshop,const QString& fixture) {
        const auto rev=workshop.revision;
        const auto plan=workshop.flow.snapshot();
        auto* open=action(workshop,"Mở STL");
        auto* preset2=action(workshop,"2 mặt + tinh");
        auto* preset4=action(workshop,"4 mặt + tinh");
        auto* cancel=action(workshop,"Hủy tác vụ");
        workshop.launchJob("wrap-stl",fixture);
        check(workshop.jobBusy && cancel->isEnabled() && !workshop.modeBox->isEnabled() &&
              !workshop.tree->isEnabled() && !workshop.projectControls->isEnabled(), "worker locks editing and enables Cancel before processing");
        for(auto* control:workshop.jobControls)
            check(!control->isEnabled(), "worker suspends toolbar and menu actions");
        open->trigger();preset2->trigger();preset4->trigger();
        workshop.dialog("ai");
        check(workshop.revision==rev && workshop.flow.snapshot()==plan,
              "locked controls cannot invalidate the in-flight revision");
        workshop.worker.waitForFinished(10000);
        check(!workshop.jobBusy && !cancel->isEnabled() && workshop.modeBox->isEnabled() &&
              open->isEnabled() && preset2->isEnabled() && preset4->isEnabled(),
              "successful worker restores controls");
        check(!workshop.surfaceOverlay.empty() && workshop.revision==rev && workshop.progressBar->value()==100,
              "successful worker result reaches the viewport on the original revision");

        const auto previousSurface=workshop.surfaceOverlay;
        workshop.launchJob("wrap-stl",fixture);
        cancel->trigger();
        workshop.worker.waitForFinished(10000);
        check(!workshop.jobBusy && !cancel->isEnabled() && open->isEnabled() &&
              workshop.statusBar()->currentMessage()=="Đã hủy tác vụ.",
              "user cancellation restores controls");
        check(workshop.revision==rev && workshop.surfaceOverlay==previousSurface,
              "cancellation preserves prior results and project revision");
    }
    static void run(const QString& fixture) {
        QMatrix4x4 persisted;
        persisted.translate(3,4,5);
        persisted.rotate(87,0,0,1);
        persisted.scale(-2,-3,-4);

        {
            Workshop workshop;
            workshop.modelFile = fixture;
            QString error;
            check(workshop.view->loadSTL(fixture, error) && workshop.view->triangleCount() > 100,
                  "sample model loads at original scale");
            check(workshop.view->modelTransform().isIdentity(), "default transform is 1:1");
            QTimer::singleShot(0, [&] {
                auto* dialog = modal();
                field<QComboBox>(dialog, "Xoay quanh trục")->setCurrentIndex(2);
                field<QDoubleSpinBox>(dialog, "Góc (độ)")->setValue(87);
                const char* scale[] = {"Tỷ lệ X", "Tỷ lệ Y", "Tỷ lệ Z"};
                const char* translation[] = {"Dịch X (mm)", "Dịch Y (mm)", "Dịch Z (mm)"};
                const char* mirror[] = {"Đối xứng X", "Đối xứng Y", "Đối xứng Z"};
                for (int i = 0; i < 3; ++i) {
                    field<QDoubleSpinBox>(dialog, scale[i])->setValue(2+i);
                    field<QDoubleSpinBox>(dialog, translation[i])->setValue(3+i);
                    field<QCheckBox>(dialog, mirror[i])->setChecked(true);
                }
                accept(dialog);
            });
            action(workshop, "Biến đổi")->trigger();
            check(workshop.view->modelTransform() == persisted, "all model fields feed the exact transform");
            check(workshop.worker.state() == QProcess::NotRunning, "model save does not calculate");
            action(workshop, "Hoàn tác")->trigger();
            check(workshop.view->modelTransform().isIdentity(), "undo restores previous transform");
            for (const auto* axis : {"X", "Y", "Z"}) {
                action(workshop, QString("Xoay %1 +90°").arg(axis))->trigger();
                check(!workshop.view->modelTransform().isIdentity(), "rotation shortcut rotates model");
                action(workshop, "Khôi phục mẫu STL gốc 1:1")->trigger();
                check(workshop.view->modelTransform().isIdentity(), "reset STL restores 1:1 geometry");
            }
            action(workshop, "Lấy gốc X0")->trigger();
            QVector3D low, high;
            check(workshop.view->modelBounds(low,high) && std::abs(low.x()) < 1e-6 &&
                  std::abs(low.y()+high.y()) < 1e-6 && std::abs(low.z()+high.z()) < 1e-6,
                  "base command sets X0 and centers Y/Z");
            action(workshop, "2 mặt + tinh")->trigger();
            check(workshop.flow.pathNames().size() == 4 && workshop.worker.state() == QProcess::NotRunning,
                  "two-face preset updates plan without calculating");
            action(workshop, "4 mặt + tinh")->trigger();
            check(workshop.flow.pathNames().size() == 6 && workshop.worker.state() == QProcess::NotRunning,
                  "four-face preset updates plan without calculating");

            checkMode(workshop,4,"shanlong4");
            workshop.modeBox->setCurrentIndex(workshop.modeBox->findData(5));
            checkMode(workshop,5,"cm600");
            check(!workshop.jobBusy, "changing toolbar mode does not calculate");
            QTimer::singleShot(0, [&] {
                auto* dialog=modal();
                auto* axes=field<QComboBox>(dialog,"Chế độ xoay");
                check(axes->currentIndex()==1 &&
                      field<QLabel>(dialog,"Hồ sơ đang dùng")->text().contains("cm600"),
                      "assistant opens with the toolbar mode and post");
                axes->setCurrentIndex(0);
                field<QComboBox>(dialog,"Chế độ phá tròn")->setCurrentIndex(0);
                accept(dialog);
            });
            action(workshop,"Trợ lý")->trigger();
            checkMode(workshop,4,"shanlong4");
            check(!workshop.jobBusy, "saving assistant mode does not calculate");

            check(workshop.machine.selectProfile("table-head"), "select 5X-only profile");
            workshop.syncMachine();workshop.changed();
            checkMode(workshop,5,"non-rtcp");
            check(workshop.machine.selectProfile("table-x"), "select 4X-only profile");
            workshop.syncMachine();workshop.changed();
            checkMode(workshop,4,"mach3");
            check(!qobject_cast<QStandardItemModel*>(workshop.modeBox->model())->item(1)->isEnabled(),
                  "4X-only profile disables incompatible 5X");
            check(workshop.machine.selectProfile("cm600"), "restore dual-mode profile");
            workshop.syncMachine();workshop.changed();
            workshop.modeBox->setCurrentIndex(workshop.modeBox->findData(5));
            checkMode(workshop,5,"cm600");

            QTimer::singleShot(0, [&] {
                auto* dialog = modal();
                auto* grid = qobject_cast<QGridLayout*>(dialog->layout());
                check(grid, "stock fields use dimension grid");
                auto* width = qobject_cast<QDoubleSpinBox*>(grid->itemAtPosition(2,3)->widget());
                check(width, "stock dimension control is editable");
                width->setValue(212.25);
                check(workshop.summary->text().contains("212.2500"), "dimensional preview updates before saving");
                accept(dialog);
            });
            action(workshop, "Phôi")->trigger();
            check(workshop.stock.high().x()-workshop.stock.low().x() == 212.25, "stock dimensions commit 1:1");
            check(workshop.worker.state() == QProcess::NotRunning, "stock save does not calculate");
            for (const auto* label : {"ISO","XY","YZ","ZX","Vừa khung","Hiện phôi","Hiện STL"}) {
                action(workshop, label)->trigger();
                check(workshop.worker.state() == QProcess::NotRunning, "viewport action does not calculate");
            }
            check(action(workshop,"Cập nhật 1-Click")->isEnabled(),
                  "one-click update action is present");
            check(action(workshop,"Nguồn thông báo Google Sheet…")->isEnabled(),
                  "update source configuration action is present");
            QTimer::singleShot(0,[&]{
                auto* dialog=modal();
                auto* source=dialog->findChild<QComboBox*>("updateSource");
                auto* address=dialog->findChild<QLineEdit*>("updateSourceValue");
                auto* autoCheck=dialog->findChild<QCheckBox*>("updateAutoCheck");
                auto* backup=dialog->findChild<QCheckBox*>("updateKeepBackup");
                check(source&&address&&autoCheck&&backup&&source->findData("local")>=0,
                      "updater exposes sources, startup check and backup options");
                source->setCurrentIndex(source->findData("manifest"));
                address->setText("https://updates.example.invalid/update-feed.json");
                autoCheck->setChecked(false);
                backup->setChecked(true);
                dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Save)->click();
            });
            action(workshop,"Cấu hình Cập nhật…")->trigger();
            const auto configDirectory=QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                .filePath(QCoreApplication::organizationName()+"/"+QCoreApplication::applicationName());
            QFile updateConfig(configDirectory+"/update_config.json");
            check(updateConfig.open(QIODevice::ReadOnly),"updater saves user configuration outside the install");
            const auto savedUpdate=QJsonDocument::fromJson(updateConfig.readAll()).object();
            updateConfig.close();
            check(savedUpdate.value("source").toString()=="manifest"&&
                  savedUpdate.value("value").toString()=="https://updates.example.invalid/update-feed.json"&&
                  !savedUpdate.value("autoCheck").toBool()&&savedUpdate.value("keepBackup").toBool(),
                  "updater settings round-trip with auto-check disabled and backup retained");

            auto* row=workshop.tree->topLevelItem(4)->child(0);
            check(row->checkState(0)==Qt::Checked,"project path has direct checkbox");
            row->setCheckState(0,Qt::Unchecked);
            check(!workshop.flow.snapshot()["paths"].toArray()[0].toObject()["active"].toBool() &&
                  !workshop.jobBusy,"project checkbox updates selection without calculating");
            QTimer::singleShot(0,[&]{
                auto* dialog=modal();
                auto* report=field<QLabel>(dialog,"Báo cáo quy chuẩn");
                check(report->text().contains("Nhấc"),"live assistant rule report");
                for(auto* button:dialog->findChildren<QPushButton*>()){
                    if(!button->text().contains("TỰ ĐỘNG TÍNH TOÀN BỘ"))continue;
                    button->click();return;
                }
                check(false,"assistant explicit calculate-all button");
            });
            action(workshop,"Trợ lý")->trigger();
            check(workshop.jobBusy,"explicit assistant button launches worker");
            const auto request=QJsonDocument::fromJson(workshop.pendingJob).object();
            const auto selected=request["workflow"].toObject()["paths"].toArray();
            check(std::all_of(selected.begin(),selected.end(),[](const auto& p){return p.toObject()["active"].toBool();}),
                  "calculate all includes unchecked paths for this job");
            workshop.cancelJob->trigger();workshop.worker.waitForFinished(10000);
            check(!workshop.jobBusy,"cancelled calculate all returns control");
            workshop.flow.setPathActive(0,true);
            workshop.refresh();
            checkWorkerLock(workshop,fixture);
            auto* surface=workshop.tree->topLevelItem(3);
            check(surface->text(0)=="Mặt dẫn"&&surface->text(1)=="Lớp bọc STL"&&
                  surface->checkState(0)==Qt::Checked,"Dựng STL exposes the guide in the project tree");
            const auto guideRevision=workshop.revision;
            surface->setCheckState(0,Qt::Unchecked);
            check(!workshop.surfaceVisible&&!workshop.surfaceOverlay.empty()&&workshop.revision==guideRevision&&!workshop.jobBusy,
                  "hiding the guide preserves mesh, jobs and computed toolpaths");
            surface->setCheckState(0,Qt::Checked);
            check(workshop.surfaceVisible,"re-enabling guide restores visualization");
            workshop.view->setModelTransform(persisted);
            workshop.saveWorkspace();
            check(QFileInfo::exists(workshop.workspacePath()), "workspace is saved outside installation");
        }

        Workshop restored;
        if (restored.loading) {
            QEventLoop wait;
            QTimer poll;
            QObject::connect(&poll,&QTimer::timeout,&wait,[&] { if (!restored.loading) wait.quit(); });
            poll.start(20);
            QTimer::singleShot(5000,&wait,&QEventLoop::quit);
            wait.exec();
        }
        check(!restored.loading && restored.modelFile == fixture, "reopened workspace loads its STL");
        check(restored.view->modelTransform() == persisted, "workspace restores full model transform");
        check(restored.stock.high().x()-restored.stock.low().x() == 212.25, "workspace restores stock dimensions");
        check(restored.flow.pathNames().size() == 6 && restored.worker.state() == QProcess::NotRunning,
              "workspace restores operations without calculating");
        checkMode(restored,5,"cm600");
    }
};

int main(int argc, char** argv) {
    QApplication app(argc,argv);
    app.setOrganizationName("ArtisanAxisQA");
    app.setApplicationName("Workshop-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    QStandardPaths::setTestModeEnabled(true);
    check(app.arguments().size() == 2 && QFileInfo::exists(app.arguments()[1]), "test receives STL fixture");
    WorkshopAudit::run(QFileInfo(app.arguments()[1]).absoluteFilePath());
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    return EXIT_SUCCESS;
}
