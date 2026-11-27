#include <QtWidgets>
#include <QJsonDocument>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include "stock.h"
#include "profiles.h"
#include "workflow.h"

void check(bool value,const char *description){ if(!value){ std::cerr << "FAIL " << description << "\n"; std::exit(1); } std::cout<<"PASS "<<description<<"\n"; }
QDialog *modal(){auto *d=qobject_cast<QDialog*>(QApplication::activeModalWidget());check(d,"dialog opened");return d;}
template <typename T> T* field(QDialog *d,const char *label){
    for(auto *layout:d->findChildren<QFormLayout*>()){
        for(int i=0;i<layout->rowCount();++i){
            auto l=layout->itemAt(i,QFormLayout::LabelRole);
            auto f=layout->itemAt(i,QFormLayout::FieldRole);
            if(!l||!f)continue;
            auto name=qobject_cast<QLabel*>(l->widget());
            if(name&&name->text()==QString::fromUtf8(label)){
                auto result=qobject_cast<T*>(f->widget());check(result,label);return result;
            }
        }
    }
    check(false,label);return nullptr;
}
void accept(QDialog *d){auto *b=d->findChild<QDialogButtonBox*>();check(b,"dialog has commit control");auto *p=b->button(QDialogButtonBox::Ok);if(!p)p=b->button(QDialogButtonBox::Save);check(p&&p->isEnabled(),"commit button enabled");p->click();}
void cancel(QDialog *d){d->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Cancel)->click();}
int main(int argc,char **argv){
    QTemporaryDir root;qputenv("XDG_CONFIG_HOME",root.path().toUtf8());
    QApplication app(argc,argv);
    app.setOrganizationName("ArtisanAxisQA");
    app.setApplicationName("Dialog-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    QStandardPaths::setTestModeEnabled(true);
    QWidget parent;

    Stock current;Stock shown;int previews=0;
    auto observe=[&](const Stock &s){shown=s;++previews;};
    QTimer::singleShot(0,[&]{
        auto *d=modal();auto *grid=qobject_cast<QGridLayout*>(d->layout());check(grid,"stock grid");
        auto *shapes=d->findChild<QComboBox*>();
        auto at=[&](int row,int col){return qobject_cast<QDoubleSpinBox*>(grid->itemAtPosition(row,col)->widget());};
        for(int i=0;i<3;++i) {
            at(i+2,1)->setValue(-10-i);
            check(shown.min[i]==-10-i,"Min column drives preview");
            at(i+2,2)->setValue(100+i);
            check(shown.max[i]==100+i,"Max column drives preview");
            at(i+2,3)->setValue(90+i);
            check(shown.max[i]-shown.min[i]==90+i,"dimension drives exact bounds");
        }
        auto *exp=at(5,2);auto *ratio=at(6,2);
        exp->setValue(2);check(shown.expansion==2&&shown.low().x()==-12&&shown.high().x()==82,"expansion affects both sides");
        check(!ratio->isEnabled(),"ratio disabled for box");
        shapes->setCurrentIndex(1);check(shown.shape=="cylinder"&&!ratio->isEnabled(),"cylinder preview, ratio disabled");
        shapes->setCurrentIndex(2);ratio->setValue(.64);check(shown.shape=="frustum"&&ratio->isEnabled()&&shown.endRatio==.64,"frustum and ratio drive preview");
        cancel(d);
    });
    check(!editStock(&parent,current,false,{},{1,0,0},observe),"cancel stock returns false");
    check(shown.shape=="box"&&shown.expansion==0&&shown.high().x()==200&&previews>=15,"cancel restores stock preview");
    QTimer::singleShot(0,[&]{auto*d=modal();auto*grid=qobject_cast<QGridLayout*>(d->layout());qobject_cast<QDoubleSpinBox*>(grid->itemAtPosition(2,3)->widget())->setValue(125);accept(d);});
    check(editStock(&parent,current,false,{},{1,0,0},observe)&&current.high().x()==125,"stock save persists dimensions");

    Workflow flow; flow.setMachine(5,[](int n){return n==4?QString("mach3"):QString("test5");});
    QTimer::singleShot(0,[&]{
        auto*d=modal();const char* labels[]={"Đường kính thân (mm)","Bán kính cầu (mm)","Góc côn một bên (°)","Chiều dài lưỡi cắt (mm)","Đường kính collet (mm)","Chiều dài nhô khỏi collet L (mm)"};
        double values[]={14,.5,14,22,24,95};
        for(int i=0;i<6;++i)field<QDoubleSpinBox>(d,labels[i])->setValue(values[i]);
        auto *selector=field<QComboBox>(d,"Dao");selector->setCurrentIndex(1);selector->setCurrentIndex(0);
        for(int i=0;i<6;++i)check(field<QDoubleSpinBox>(d,labels[i])->value()==values[i],"tool selector draft round trip");
        accept(d);
    });
    check(flow.editTools(&parent),"tool accepts");
    auto tool=flow.snapshot().value("tools").toArray().first().toObject();
    check(tool.value("diameter").toDouble()==14&&tool.value("tipRadius").toDouble()==.5&&tool.value("singleSideHalfAngle").toDouble()==14&&tool.value("flute").toDouble()==22&&tool.value("holderDiameter").toDouble()==24&&tool.value("overhang").toDouble()==95&&tool.value("outLen").toDouble()==95,"every tool control serialized");
    QTimer::singleShot(0,[&]{auto*d=modal();check(field<QDoubleSpinBox>(d,"Điểm quanh chu vi")->maximum()==96&&field<QDoubleSpinBox>(d,"Lát theo trục")->maximum()==128,"surface range matches worker");field<QDoubleSpinBox>(d,"Điểm quanh chu vi")->setValue(70);field<QDoubleSpinBox>(d,"Lát theo trục")->setValue(100);accept(d);});
    check(flow.editSurface(&parent),"surface accepts");
    check(flow.snapshot().value("wrapColumns").toInt()==70&&flow.snapshot().value("wrapStacks").toInt()==100,"surface controls serialized");
    QTimer::singleShot(0,[&]{auto*d=modal();field<QCheckBox>(d,"Đưa vào công việc")->setChecked(false);field<QDoubleSpinBox>(d,"Bước dịch (mm)")->setValue(4);field<QDoubleSpinBox>(d,"Lượng dư (mm)")->setValue(4.5);accept(d);});
    check(flow.editPath(&parent,0),"path accepts");
    auto path=flow.snapshot().value("paths").toArray().first().toObject();
    check(!path.value("active").toBool()&&path.value("step").toDouble()==4&&path.value("allowance").toDouble()==4.5,"three path controls serialized");
    QTimer::singleShot(0,[&]{
        auto*d=modal();field<QPlainTextEdit>(d,"Mô tả công việc")->setPlainText("Phá 2 mặt, 5 trục, dao phá Ø10 dài 55 mm, tinh góc 15, chiều dài dao tinh 77, bước tinh 0.21, dư cận tinh 1.3");
        QPushButton* parse=nullptr;for(auto *b:d->findChildren<QPushButton*>())if(b->text()=="Đọc mô tả")parse=b;
        check(parse,"AI parse button");parse->click();
        check(field<QComboBox>(d,"Phá mặt")->currentIndex()==0,"AI parsed face selector");
        check(field<QComboBox>(d,"Chế độ xoay")->currentIndex()==1,"AI parsed axes selector");
        check(field<QComboBox>(d,"Dao phá")->currentIndex()==1&&field<QComboBox>(d,"Dao tinh")->currentIndex()==1,"AI parsed two tool selectors");
        check(field<QDoubleSpinBox>(d,"L dao phá (mm)")->value()==55&&field<QDoubleSpinBox>(d,"L dao tinh (mm)")->value()==77,"AI parsed both lengths");
        check(field<QDoubleSpinBox>(d,"Bước tinh (mm)")->value()==.21&&field<QDoubleSpinBox>(d,"Dư cận tinh (mm)")->value()==1.3,"AI parsed step and allowance");
        field<QDoubleSpinBox>(d,"Bước tinh (mm)")->setValue(.22);parse->click();
        check(field<QDoubleSpinBox>(d,"Bước tinh (mm)")->value()==.22,"AI manually edited field is retained");
        accept(d);
    });
    check(flow.editAI(&parent),"AI accepts");
    auto snapshot=flow.snapshot();check(snapshot.value("axes").toInt()==5&&snapshot.value("faces").toInt()==2,"AI selectors alter workflow");
    check(snapshot.value("tools").toArray().at(1).toObject().value("overhang").toDouble()==55&&snapshot.value("tools").toArray().at(4).toObject().value("overhang").toDouble()==77,"AI lengths serialized into selected tools");
    MachineProfiles machines(&parent);
    QTimer::singleShot(0,[&]{
        auto*d=modal();
        auto* ox=d->findChild<QDoubleSpinBox*>("workOffsetX"),*oy=d->findChild<QDoubleSpinBox*>("workOffsetY"),*oz=d->findChild<QDoubleSpinBox*>("workOffsetZ");
        check(ox&&oy&&oz&&ox->value()==264.607&&oy->value()==455.104&&oz->value()==-470.090,"CM600 XYZ reference displayed");
        auto* confirmed=d->findChild<QCheckBox*>();
        check(confirmed&&!confirmed->isChecked(),"reference defaults unconfirmed");
        confirmed->setChecked(true);
        ox->setValue(264.608);oy->setValue(455.103);oz->setValue(-470.089);
        check(!confirmed->isChecked(),"editing datum clears prior confirmation");
        confirmed->setChecked(true);
        field<QLineEdit>(d,"Tên máy")->setText("QA machine");
        field<QDoubleSpinBox>(d,"Tay đòn")->setValue(236.982);
        auto* posts=field<QComboBox>(d,"Post 4 trục");posts->setCurrentIndex(posts->findData("mach3"));
        auto* tabs=d->findChild<QTabWidget*>();
        check(tabs && tabs->count()==3,"all machine tabs available");
        auto* catalog=qobject_cast<QPlainTextEdit*>(tabs->widget(2));check(catalog&&catalog->isReadOnly()&&!catalog->toPlainText().isEmpty(),"post catalog is read-only reference");
        accept(d);
    });
    check(machines.edit(&parent),"machine profile accepts");
    const auto p=machines.current();
    check(p.value("name")=="QA machine"&&p.value("post4")=="mach3"&&std::abs(p.value("swingarmMm").toDouble()-236.982)<1e-12,"machine controls persisted in model");
    check(p.value("offsetConfirmed").toBool()&&p.value("workOffsetMachine").toArray()==QJsonArray{264.608,455.103,-470.089},"datum and confirmation persisted");
    MachineProfiles restored(&parent);
    check(restored.current()==p,"machine profile reload round trip");
    check(flow.restore(snapshot),"workflow snapshot can be reopened");
    check(flow.snapshot().value("axes").toInt()==5&&flow.snapshot().value("tools")==snapshot.value("tools"),
          "restored machining settings persist");
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).removeRecursively();
    return 0;
}
