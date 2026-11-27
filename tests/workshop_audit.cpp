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
