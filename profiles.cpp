#include "profiles.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTabWidget>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>
#include <cmath>

namespace {
using Obj = QJsonObject;
using Arr = QJsonArray;
QString configFile() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/machines-v1.json";
}
QString json(const Obj& o) {
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Indented));
}
bool finiteNumber(const QJsonValue& v) { return v.isDouble() && std::isfinite(v.toDouble()); }
bool vector3(const QJsonValue& v) {
    const Arr a = v.toArray();
    return a.size() == 3 && finiteNumber(a[0]) && finiteNumber(a[1]) && finiteNumber(a[2]);
}
QVector3D vector(const QJsonValue& v) {
    const Arr a = v.toArray();
    return QVector3D(float(a.at(0).toDouble()), float(a.at(1).toDouble()), float(a.at(2).toDouble()));
}
int locate(const Arr& profiles, const QString& id) {
    for (qsizetype i=0; i<profiles.size(); ++i)
        if (profiles[i].toObject()["id"].toString() == id) return int(i);
    return -1;
}
Obj post(const char* id, const char* name, int axes, const char* mode) {
    return {{"id", id}, {"label", QString::fromUtf8(name)}, {"axes", axes},
            {"mode", mode}, {"words", axes == 4 ? "XYZA" : "XYZAB"},
            {"header", "(XUAN DIEU - huynhgiaphattc@gmail.com)"}, {"qualification", "reference"}};
}
Obj axis(const QString& word, const QString& role, Arr direction) {
    return {{"word",word}, {"role",role}, {"direction",direction}, {"origin",Arr{0,0,0}}, {"sign",1}};
}
Obj templ(const QString& id, const QString& name, const QString& geometry,
          const QString& post4,const QString& post5,const QString& comp,bool cm=false) {
    const bool four = geometry.startsWith("4");
    const bool headA = geometry == "head-head";
    const bool tableB = geometry == "table-table";
    Arr rotary{axis("A",headA ? "head" : "table", geometry == "4-table-Y" ? Arr{0,1,0} : Arr{1,0,0})};
    if (!four) rotary.append(axis("B", tableB ? "table" : "head", Arr{0,1,0}));
    Obj limits;
    for (QChar c : QString(four ? "XYZA" : "XYZAB")) limits[QString(c)] = QJsonValue::Null;
    if (cm) limits["Z"] = Arr{-450,200};
    return {{"id",id},{"name",name},{"geometry",geometry},{"rotary",rotary},{"limits",limits},
            {"swingarmMm", cm ? 232.918 : 0.0},{"rtcpMode",comp},{"rtcpRegister",cm ? "H1" : ""},
            {"controllerMode",cm ? "7" : ""},{"workOffsetMachine",cm?QJsonValue(Arr{264.607,455.104,-470.090}):QJsonValue(QJsonValue::Null)},{"offsetConfirmed",false},{"post4",post4},{"post5",post5},
            {"referenceAxis",Obj{{"origin",Arr{0,0,0}},{"direction",geometry == "4-table-Y" ? Arr{0,1,0} : Arr{1,0,0}}}},
            {"customPosts",Arr{}}};
}
Arr defaults() {
    return {
        templ("cm600","Shanlong CM600 · Xuân Diệu CNC","table-head","shanlong4","cm600","G43.4",true),
        templ("table-x","4 trục · bàn A dọc X","4-table-X","mach3","","off"),
        templ("table-y","4 trục · bàn A dọc Y","4-table-Y","mach3","","off"),
        templ("head-head","5 trục · đầu–đầu","head-head","","non-rtcp","cam"),
        templ("table-table","5 trục · bàn–bàn","table-table","","non-rtcp","cam"),
        templ("table-head","5 trục · bàn A + đầu B","table-head","","non-rtcp","cam")
    };
}
Arr catalog(const Obj& o) {
    Arr result = MachineProfiles::postCatalog();
    for (const auto v : o["customPosts"].toArray()) result.append(v);
    return result;
}
QStringList geometries() {return {"4-table-X","4-table-Y","table-head","head-head","table-table"};}
QStringList modes() {return {"off","cam","G43.4","G43.5","TRAORI"};}
QString modeOf(const Obj& p,const QString& id) {
    for (const auto v : catalog(p)) if(v.toObject()["id"].toString()==id) return v.toObject()["mode"].toString();
    return {};
}
QString requiredWords(int n) {return n == 4 ? "XYZA" : "XYZAB";}
}

QJsonArray MachineProfiles::postCatalog() {
    return {post("shanlong4","Shanlong 4X",4,"off"),post("jd45","ArtForm JD45",4,"off"),
            post("jd50","ArtForm JD50",4,"off"),post("weihong4","Weihong / NcStudio 4X",4,"off"),
            post("mach3","Mach3 4X",4,"off"),post("syntec4","Syntec 4X",4,"off"),
            post("richauto","RichAuto DSP 4X",4,"off"),post("cm600","Shanlong CM600",5,"G43.4"),
            post("syntec5","Syntec 5X · mẫu G43.4",5,"G43.4"),post("fanuc5","Fanuc 5X · mẫu G43.4",5,"G43.4"),
            post("fanuc-vector","Fanuc 5X · mẫu G43.5",5,"G43.5"),post("lnc5","LNC 5X · mẫu",5,"G43.4"),
            post("weihong5","Weihong 5X · mẫu",5,"G43.4"),post("traori","Siemens 5X · TRAORI",5,"TRAORI"),
            post("non-rtcp","5X · CAM giải động học",5,"cam")};
}

QString MachineProfiles::validate(const Obj& o) {
    if (o["id"].toString().trimmed().isEmpty() || o["name"].toString().trimmed().isEmpty())
        return "Cần mã và tên hồ sơ máy.";
    const auto g = o["geometry"].toString();
    if (!geometries().contains(g)) return "Kiểu máy không hợp lệ.";
    const int n = g.startsWith("4") ? 4 : 5;
    if (!finiteNumber(o["swingarmMm"]) || o["swingarmMm"].toDouble()<0) return "Tay đòn phải là số không âm (mm).";
    const Arr rotations=o["rotary"].toArray();
    if(rotations.size()!=n-3) return "Số khớp quay chưa khớp cấu trúc máy.";
    for(int i=0;i<rotations.size();++i) {
        Obj a=rotations[i].toObject();
        QString role=(i==0 ? g=="head-head" : g!="table-table") ? "head" : "table";
        if(a["word"].toString()!=(i==0 ? "A" : "B") || a["role"].toString()!=role)
            return "Tên hoặc vai trò của khớp quay chưa khớp cấu trúc máy.";
        if(!vector3(a["origin"]) || !vector3(a["direction"]) || !qFuzzyCompare(vector(a["direction"]).length(),1.0f))
            return "Mỗi khớp cần tâm XYZ và vectơ hướng có độ dài 1.";
        const double sign=a["sign"].toDouble(0);
        if(sign!=1 && sign!=-1) return "Chiều quay phải là +1 hoặc −1.";
        if(n==4) {
            const auto direction=vector(a["direction"]);
            const auto expected=g=="4-table-Y"? QVector3D(0,1,0):QVector3D(1,0,0);
            if((direction-expected).length()>1e-5f) return "Hướng khớp A phải theo kiểu dọc X/Y đã chọn. Đổi chiều bằng sign.";
        }
    }
    if(o.contains("workOffsetMachine")&&!o["workOffsetMachine"].isNull()&&!vector3(o["workOffsetMachine"]))
        return "workOffsetMachine cần [X,Y,Z] theo mm hoặc null.";
    const Obj ref=o["referenceAxis"].toObject();
    if(!vector3(ref["origin"]) || !vector3(ref["direction"]) || !qFuzzyCompare(vector(ref["direction"]).length(),1.0f))
        return "Trục tham chiếu phôi cần tâm XYZ và vectơ đơn vị.";
    const auto stockDir=vector(ref["direction"]);
    if ((stockDir-QVector3D(1,0,0)).length()>1e-5f && (stockDir-QVector3D(0,1,0)).length()>1e-5f)
        return "Chọn hướng dọc phôi +X hoặc +Y tại referenceAxis.direction.";
    const Obj limits=o["limits"].toObject();
    for(QChar c : requiredWords(n)) {
        const auto v=limits.value(QString(c));
        if(v.isNull()) continue;
        const Arr a=v.toArray();
        if(a.size()!=2 || !finiteNumber(a[0]) || !finiteNumber(a[1]) || a[0].toDouble()>=a[1].toDouble())
            return "Giới hạn "+QString(c)+" cần [min, max] hoặc null khi chưa đo.";
    }
    const QString mode=o["rtcpMode"].toString();
    if(!modes().contains(mode) || (n==4 && mode!="off")) return "Chế độ bù động học chưa khớp máy.";
    const Arr posts=catalog(o);
    QSet<QString> ids;
    bool has4=false,has5=false;
    if(!o["customPosts"].isArray()) return "customPosts phải là danh sách.";
    for(const auto item:posts) {
        Obj p=item.toObject(); const QString id=p["id"].toString(); const auto axes=p["axes"].toInt();
        if(id.isEmpty() || ids.contains(id) || (axes!=4 && axes!=5) || p["label"].toString().isEmpty() ||
           !modes().contains(p["mode"].toString()) || (axes==4 && p["mode"].toString()!="off") ||
           p["words"].toString()!=requiredWords(axes)) return "Post cần mã duy nhất, tên, mode và tập trục đúng.";
        ids.insert(id);
        if(id==o["post4"].toString() && axes==4) has4=true;
        if(id==o["post5"].toString() && axes==5 && p["mode"].toString()==mode) has5=true;
    }
    if((n==4 || !o["post4"].toString().isEmpty()) && !has4) return "Post 4X chưa hợp lệ.";
    if(n==5 && !has5) return "Post 5X và chế độ RTCP phải tương thích.";
    if(n==4 && !o["post5"].toString().isEmpty()) return "Máy 4 trục chỉ dùng post 4X.";
    return {};
}

MachineProfiles::MachineProfiles(QWidget* p):QObject(p),records_(defaults()),activeId_("cm600"){
    QFile f(configFile());
    if(!f.exists()) return;
    if(!f.open(QIODevice::ReadOnly)){ warning_=f.errorString(); return; }
    const auto doc=QJsonDocument::fromJson(f.readAll()).object();
    const auto a=doc["machines"].toArray(); const auto active=doc["active"].toString();
    if(doc["version"].toInt()!=1 || locate(a,active)<0){warning_="Hồ sơ lưu không hợp lệ. Đang dùng bộ mẫu.";return;}
    QSet<QString> seen;
    for(const auto v:a) {
        const auto rec=v.toObject(); const auto id=rec["id"].toString();
        if(!validate(rec).isEmpty() || seen.contains(id)){warning_="Hồ sơ lưu bị lỗi. Đang dùng bộ mẫu.";return;}
        seen.insert(id);
    }
    records_=a; activeId_=active;
}
bool MachineProfiles::store(const Arr& a,const QString& id,QString* error) const {
    const QString path=configFile();
    if(!QDir().mkpath(QFileInfo(path).absolutePath())){ if(error)*error="Không thể tạo thư mục cấu hình."; return false; }
    QSaveFile f(path);
    if(!f.open(QIODevice::WriteOnly)){if(error)*error=f.errorString();return false;}
    const auto data=QJsonDocument(Obj{{"version",1},{"active",id},{"machines",a}}).toJson();
    if(f.write(data)!=data.size() || !f.commit()){if(error)*error=f.errorString();return false;}
    return true;
}
Obj MachineProfiles::current() const {
    auto result=records_.at(locate(records_,activeId_)).toObject();
    if(activeId_=="cm600"&&!result.value("workOffsetMachine").isArray()){
        result["workOffsetMachine"]=Arr{264.607,455.104,-470.090};
        result["offsetConfirmed"]=false;
    }
    return result;
}
QString MachineProfiles::name() const {return current()["name"].toString();}
int MachineProfiles::axisCount() const {return 3+int(current()["rotary"].toArray().size());}
QVector3D MachineProfiles::stockAxisDirection() const {return vector(current()["referenceAxis"].toObject()["direction"]);}
QVector3D MachineProfiles::rotaryOrigin() const {
    const Obj p=current();
    const Obj a=p["rotary"].toArray().first().toObject();
    return vector(a["role"].toString()=="table"?a["origin"]:p["referenceAxis"].toObject()["origin"]);
}
QVector3D MachineProfiles::rotaryDirection() const {
    const Obj p=current();
    const Obj a=p["rotary"].toArray().first().toObject();
    return vector(a["role"].toString()=="table"?a["direction"]:p["referenceAxis"].toObject()["direction"]);
}
QString MachineProfiles::postForAxes(int n) const {
    if(n==4) return current()["post4"].toString();
    return n==5 && axisCount()==5 ? current()["post5"].toString() : QString();
}
QString MachineProfiles::loadWarning() const { return warning_; }
bool MachineProfiles::selectProfile(const QString& id,QString* error) {
    if(locate(records_,id)<0){if(error)*error="Không tìm thấy hồ sơ máy.";return false;}
    if(!store(records_,id,error))return false;
    activeId_=id;return true;
}
bool MachineProfiles::edit(QWidget* parent) {
    QDialog dlg(parent);
    dlg.setWindowTitle("Hồ sơ máy và bộ xuất G-Code");
    dlg.resize(730,640);
    QVBoxLayout outer(&dlg);
    auto* top=new QHBoxLayout();
    outer.addLayout(top);
    auto* selector=new QComboBox();
    auto* clone=new QPushButton("Nhân bản");
    top->addWidget(new QLabel("Hồ sơ máy"));
    top->addWidget(selector,1);
    top->addWidget(clone);
    auto* tabs=new QTabWidget();
    outer.addWidget(tabs,1);
    auto* basic=new QWidget();
    auto* form=new QFormLayout(basic);
    auto* nameEdit=new QLineEdit();
    auto* layoutKind=new QLabel();
    auto* arm=new QDoubleSpinBox();
    arm->setRange(0,100000);arm->setDecimals(4);arm->setSuffix(" mm");
    auto* post4=new QComboBox();
    auto* post5=new QComboBox();
    auto* modeLabel=new QLabel();
    auto* mode7=new QLabel();
    auto* hReg=new QLabel();
    auto* confirmed=new QCheckBox("Đã đối chiếu với gốc G54 và tọa độ cơ khí");
    auto* offset=new QWidget;
    auto* offsetRow=new QHBoxLayout(offset);offsetRow->setContentsMargins(0,0,0,0);
    QDoubleSpinBox* workOffset[3];
    for(int i=0;i<3;++i){
        auto* input=new QDoubleSpinBox;
        input->setDecimals(3);input->setRange(-100000,100000);input->setSuffix(" mm");
        const auto axisName=QString("XYZ").mid(i,1);input->setAccessibleName("Tâm quay hệ máy "+axisName);
        input->setObjectName("workOffset"+axisName);
        offsetRow->addWidget(new QLabel(axisName));offsetRow->addWidget(input,1);workOffset[i]=input;
        QObject::connect(input,&QDoubleSpinBox::valueChanged,&dlg,[confirmed]{confirmed->setChecked(false);});
    }
    form->addRow("Tên máy",nameEdit);
    form->addRow("Cấu trúc",layoutKind);
    form->addRow("Tay đòn",arm);
    form->addRow("Post 4 trục",post4);
    form->addRow("Post 5 trục",post5);
    form->addRow("Bù động học",modeLabel);
    form->addRow("Chế độ trên bộ điều khiển",mode7);
    form->addRow("Thanh ghi bù dao",hReg);
    form->addRow("Tâm quay trong hệ máy",offset);
    form->addRow("Xác nhận hiệu chuẩn",confirmed);
    auto* info=new QLabel("CM600 điền tọa độ tham chiếu của xưởng. Hãy xác nhận sau khi đối chiếu tại máy. Bộ xuất hỗ trợ 4X bàn A và 5X bàn A + đầu B theo phương ngữ đã triển khai.");
    info->setWordWrap(true);form->addRow(info);
    tabs->addTab(basic,"Hồ sơ và post");

    auto* adv=new QWidget();
    auto* advancedLayout=new QVBoxLayout(adv);
    auto* help=new QLabel("rotary ghi các khớp theo thứ tự động học tại tư thế 0, cùng hệ tọa độ với phôi. origin là tâm mm, direction là vectơ đơn vị, sign là +1/−1. limits khai báo [min,max] theo mm hoặc độ, null cho hành trình chưa đo. referenceAxis xác định hướng dọc phôi +X hoặc +Y. Tâm quay trong hệ máy chỉnh ở thẻ Hồ sơ và post. Thêm mẫu post tại customPosts rồi bấm Nạp danh sách post.");
    help->setWordWrap(true);
    advancedLayout->addWidget(help);
    auto* editor=new QPlainTextEdit();
    advancedLayout->addWidget(editor);
    auto* reloadPosts=new QPushButton("Nạp danh sách post");
    advancedLayout->addWidget(reloadPosts);
    tabs->addTab(adv,"Tâm quay, giới hạn và post tùy chỉnh");
    auto* library=new QPlainTextEdit();
    library->setPlainText(QString::fromUtf8(QJsonDocument(postCatalog()).toJson(QJsonDocument::Indented)));
    library->setReadOnly(true);
    tabs->addTab(library,"Danh mục post tham chiếu");
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("Lưu và dùng máy này");
    buttons->button(QDialogButtonBox::Cancel)->setText("Hủy");
    outer.addWidget(buttons);

    Arr draft=records_;
    int index=locate(draft,activeId_);
    auto setPosts=[&](const Obj& obj,const QString& p4,const QString& p5){
        post4->clear();post5->clear();
        post4->addItem("Chưa cấu hình",QString());
        post5->addItem("Chưa cấu hình",QString());
        for(const auto v:catalog(obj)){
            const Obj p=v.toObject();
            auto* box=p["axes"].toInt()==4?post4:post5;
            box->addItem(p["label"].toString(),p["id"].toString());
        }
        post4->setCurrentIndex(qMax(0,post4->findData(p4)));
        post5->setCurrentIndex(qMax(0,post5->findData(p5)));
        post5->setEnabled(!obj["geometry"].toString().startsWith("4"));
        modeLabel->setText(post5->isEnabled()?modeOf(obj,post5->currentData().toString()):"off");
    };
    auto load=[&](){
        Obj o=draft[index].toObject();
        nameEdit->setText(o["name"].toString());
        layoutKind->setText(o["geometry"].toString());
        arm->setValue(o["swingarmMm"].toDouble());
        mode7->setText(o["controllerMode"].toString().isEmpty()?"Theo bộ điều khiển":o["controllerMode"].toString());
        hReg->setText(o["rtcpMode"].toString()=="G43.4"?"H1":"Theo post");
        auto values=o["workOffsetMachine"].toArray();
        const bool stored=values.size()==3;
        if(!stored&&o["id"].toString()=="cm600")values=Arr{264.607,455.104,-470.090};
        for(int i=0;i<3;++i){QSignalBlocker guard(workOffset[i]);workOffset[i]->setValue(values.size()==3?values[i].toDouble():0.);}
        confirmed->setChecked(stored&&o["offsetConfirmed"].toBool(true));
        setPosts(o,o["post4"].toString(),o["post5"].toString());
        for(const auto key:{"id","name","swingarmMm","post4","post5","controllerMode","rtcpRegister","rtcpMode","workOffsetMachine","offsetConfirmed"}) o.remove(key);
        editor->setPlainText(json(o));
    };
    auto parse=[&](Obj& o){
        QJsonParseError e;
        const auto doc=QJsonDocument::fromJson(editor->toPlainText().toUtf8(),&e);
        if(e.error!=QJsonParseError::NoError || !doc.isObject()){
            QMessageBox::warning(&dlg,"Kiểm tra JSON","Cần một đối tượng JSON hợp lệ. "+e.errorString());
            return false;
        }
        o=doc.object();
        return true;
    };
    auto snapshot=[&](){
        Obj o;
        if(!parse(o)) return false;
        o["id"]=draft[index].toObject()["id"];
        o["name"]=nameEdit->text().trimmed();o["swingarmMm"]=arm->value();
        const auto prior=draft[index].toObject();
        o["controllerMode"]=prior["controllerMode"];o["rtcpRegister"]=prior["rtcpRegister"];
        o["workOffsetMachine"]=Arr{workOffset[0]->value(),workOffset[1]->value(),workOffset[2]->value()};
        o["offsetConfirmed"]=confirmed->isChecked();
        o["post4"]=post4->currentData().toString();
        const bool four=o["geometry"].toString().startsWith("4");
        o["post5"]=four?QString():post5->currentData().toString();
        o["rtcpMode"]=four?QStringLiteral("off"):modeOf(o,o["post5"].toString());
        const auto error=validate(o);
        if(!error.isEmpty()){QMessageBox::warning(&dlg,"Kiểm tra hồ sơ",error);return false;}
        draft[index]=o;selector->setItemText(index,o["name"].toString());return true;
    };
    for(const auto v:draft) selector->addItem(v.toObject()["name"].toString());
    selector->setCurrentIndex(index);load();
    QObject::connect(post5,&QComboBox::currentIndexChanged,&dlg,[&](){
        const Obj advanced=QJsonDocument::fromJson(editor->toPlainText().toUtf8()).object();
        auto mode=post5->isEnabled()?modeOf(advanced,post5->currentData().toString()):QString("off");
        modeLabel->setText(mode);hReg->setText(mode=="G43.4"?"H1":"Theo post");
    });
    QObject::connect(reloadPosts,&QPushButton::clicked,&dlg,[&](){
        Obj o;if(!parse(o))return;
        setPosts(o,post4->currentData().toString(),post5->currentData().toString());
        layoutKind->setText(o["geometry"].toString());
    });
    QObject::connect(selector,&QComboBox::currentIndexChanged,&dlg,[&](int next){
        if(next<0 || next==index)return;
        if(!snapshot()){QSignalBlocker b(selector);selector->setCurrentIndex(index);return;}
        index=next;load();
    });
    QObject::connect(clone,&QPushButton::clicked,&dlg,[&](){
        if(!snapshot())return;
        Obj o=draft[index].toObject();
        bool ok=false;const QString title=QInputDialog::getText(&dlg,"Nhân bản hồ sơ","Tên máy mới",QLineEdit::Normal,o["name"].toString()+" (2)",&ok).trimmed();
        if(!ok || title.isEmpty())return;
        o["id"]=QUuid::createUuid().toString(QUuid::WithoutBraces);o["name"]=title;
        draft.append(o);
        QSignalBlocker blocker(selector);
        index=int(draft.size()-1);selector->addItem(title);selector->setCurrentIndex(index);load();
    });
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
    QObject::connect(buttons,&QDialogButtonBox::accepted,&dlg,[&](){
        if(!snapshot())return;
        const QString id=draft[index].toObject()["id"].toString();
        QString error;
        if(!store(draft,id,&error)){QMessageBox::warning(&dlg,"Lưu hồ sơ máy",error);return;}
        records_=draft;activeId_=id;warning_.clear();dlg.accept();
    });
    return dlg.exec()==QDialog::Accepted;
}
