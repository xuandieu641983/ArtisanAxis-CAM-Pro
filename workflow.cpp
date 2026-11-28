#include "workflow.h"
#include "stock.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
struct Form {
    QDialog d;
    QVBoxLayout outer;
    QFormLayout *fields;
    Form(QWidget *parent,const QString& title):d(parent),outer(&d),fields(new QFormLayout) {
        d.setWindowTitle(title); d.setMinimumWidth(440); outer.addLayout(fields);
    }
    void scroll(){
        auto* body=new QWidget;
        outer.removeItem(fields);
        body->setLayout(fields);
        auto* area=new QScrollArea;
        area->setWidgetResizable(true);
        area->setWidget(body);
        outer.addWidget(area);
        d.resize(670,650);
    }
    bool accept() {
        auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Ok)->setText("✓ Lưu && Đóng");
        buttons->button(QDialogButtonBox::Cancel)->setText("Hủy");
        outer.addWidget(buttons);
        QObject::connect(buttons,&QDialogButtonBox::accepted,&d,&QDialog::accept);
        QObject::connect(buttons,&QDialogButtonBox::rejected,&d,&QDialog::reject);
        return d.exec()==QDialog::Accepted;
    }
    QDoubleSpinBox *number(QString label,double value,double lo,double hi,int decimals=3) {
        auto *w=new QDoubleSpinBox; w->setDecimals(decimals); w->setRange(lo,hi);
        w->setValue(value); fields->addRow(label,w); return w;
    }
    QComboBox *choice(QString label,const QStringList& options,int current) {
        auto *w=new QComboBox; w->addItems(options); w->setCurrentIndex(current);
        fields->addRow(label,w);return w;
    }
};
QString normalized(QString text) {
    text=text.toLower().normalized(QString::NormalizationForm_D);
    text.remove(QRegularExpression("\\p{M}"));text.replace(QChar(0x111),'d');text.replace(QRegularExpression(R"((?<=\d),(?=\d))"), ".");
    return text;
}
}

Workflow::Workflow() {
    tools_={{"Phá Ø12.7",12.7,0,0,85,85},{"Phá cầu Ø10",10,5,0,70,70},
            {"Phá cầu Ø6",6,3,0,55,55},{"Tinh R0.4 · 10°",8,.4,10,80,80},
            {"Tinh R0.4 · 15°",6,.4,15,80,80}};
    preset(4);
}
void Workflow::invalidateAll(){ for(auto &p:paths_) ++p.revision; }
QString Workflow::currentPost(int axes) const {
    return post_ && axes>=4 && axes<=machineAxes_ ? post_(axes) : QString();
}
void Workflow::syncPathAxes() {
    for(auto& path:paths_) {
        const int axes=path.angle<0 ? axes_ : 4;
        if(path.axis==axes)continue;
        path.axis=axes;
        ++path.revision;
    }
}
bool Workflow::setAxes(int axes, bool manual) {
    if(currentPost(axes).isEmpty())return false;
    if(manual)manual_.insert("axes");
    axes_=axes;
    syncPathAxes();
    return true;
}
void Workflow::setMachine(int count,std::function<QString(int)> post) {
    machineAxes_=count; post_=std::move(post);
    if(axes_>count) axes_=count;
    if(currentPost(axes_).isEmpty()&&!currentPost(9-axes_).isEmpty())axes_=9-axes_;
    syncPathAxes();
    invalidateAll();
}
void Workflow::setStock(double x,double y,double z,double r,double lift){
    if(x==stockX_&&y==stockY_&&z==stockZ_&&r==corner_&&lift==retract_)return;
    stockX_=x;stockY_=y;stockZ_=z;corner_=r;retract_=lift;updateReach();invalidateAll();
}
void Workflow::updateReach() {
    for(auto &p:paths_) if(p.angle>=0) {
        const double half=(p.angle==90||p.angle==270?stockY_:stockZ_)*.5;
        p.minZ=std::max(faces_==2?-1.:1.,half-tools_.at(p.tool).overhang+6.);
    }
}
void Workflow::preset(int faces) {
    faces_=faces==2?2:4;paths_.clear();
    const int orders[]={0,180,90,270};
    for(int i=0;i<faces_;++i){
        paths_.append({"Phá mặt "+QString::number(orders[i])+"°",4,rough_,double(orders[i]),3.,2.,0.});
        paths_.last().feed=450;
    }
    paths_.append({"Phá tròn cận tinh",axes_,rough_,-1,2.5,semiAllowance_,0});
    paths_.last().feed=550;
    paths_.append({"Chạy tinh",axes_,finish_,-1,finishStep_,0,0});
    updateReach();
}
QStringList Workflow::toolNames()const{QStringList out;for(const auto &t:tools_)out<<t.name;return out;}
QStringList Workflow::pathNames()const{QStringList out;for(const auto &p:paths_)out<<p.name;return out;}
void Workflow::syncLength(int id,double v) {
    if(tools_[id].overhang==v&&tools_[id].outLen==v)return;
    tools_[id].overhang=v;tools_[id].outLen=v;
    for(auto &p:paths_)if(p.tool==id)++p.revision;
    updateReach();
}
bool Workflow::editTools(QWidget *parent) {
    Form f(parent,"Thư viện dao");auto draft=tools_;
    auto *id=f.choice("Dao",toolNames(),0);
    auto *dia=f.number("Đường kính thân (mm)",draft[0].diameter,.01,500);
    auto *radius=f.number("Bán kính cầu (mm)",draft[0].radius,0,250);
    auto *angle=f.number("Góc côn một bên (°)",draft[0].angle,0,89,2);
    auto* flute=f.number("Chiều dài lưỡi cắt (mm)",draft[0].flute,.1,2000);
    auto* holder=f.number("Đường kính collet (mm)",draft[0].holderDiameter,.1,500);
    auto *length=f.number("Chiều dài nhô khỏi collet L (mm)",draft[0].overhang,.1,2000);
    int sel=0;
    auto store=[&]{auto &t=draft[sel];t.diameter=dia->value();t.radius=radius->value();t.angle=angle->value();t.overhang=t.outLen=length->value();t.flute=flute->value();t.holderDiameter=holder->value();};
    QObject::connect(id,&QComboBox::currentIndexChanged,&f.d,[&](int n){
        store();sel=n;const auto &t=draft[n];dia->setValue(t.diameter);radius->setValue(t.radius);angle->setValue(t.angle);length->setValue(t.overhang);flute->setValue(t.flute);holder->setValue(t.holderDiameter);
    });
    if(!f.accept())return false;
    store();
    for(const auto &t:draft)if(t.radius*2>t.diameter||t.flute>t.overhang||t.holderDiameter<t.diameter){QMessageBox::warning(parent,"Dao","Kiểm tra bán kính cầu, chiều dài lưỡi, chiều nhô và đường kính collet.");return false;}
    for(int i=0;i<draft.size();++i) {
        const auto &old=tools_[i];const auto &next=draft[i];
        if(old.overhang!=next.overhang)manual_.insert("length"+QString::number(i));
        if(old.diameter!=next.diameter||old.radius!=next.radius||old.angle!=next.angle||old.overhang!=next.overhang||old.flute!=next.flute||old.holderDiameter!=next.holderDiameter){
            tools_[i]=next;for(auto &p:paths_)if(p.tool==i)++p.revision;
        }
    }
    updateReach();return true;
}
bool Workflow::editAI(QWidget *parent,Stock* stock,const std::function<double(const Stock&)>& radius,AiAction* action){
    if(action)*action=Save;
    Form f(parent,"Trợ lý AI · Lập cấu hình");auto locks=manual_;bool applying=false;
    auto *prompt=new QPlainTextEdit(prompt_);prompt->setMaximumHeight(90);prompt->setPlaceholderText("Phá 2 mặt, 4 trục, dao phá Ø10 dài 45 mm, tinh R0.4 góc 15, bước tinh 0.20 mm");
    f.fields->addRow("Mô tả công việc",prompt);
    auto *parse=new QPushButton("✨ Đọc & Áp Dụng");f.fields->addRow(parse);
    auto *faces=f.choice("Phá mặt",{"2 mặt","4 mặt"},faces_==2?0:1);
    QStringList axisList={"4 trục"};if(machineAxes_>=5)axisList<<"5 trục";
    auto *axes=f.choice("Chế độ xoay",axisList,axes_==5?1:0);
    auto* axisItems=qobject_cast<QStandardItemModel*>(axes->model());
    for(int i=0;i<axes->count();++i)axisItems->item(i)->setEnabled(!currentPost(i+4).isEmpty());
    auto *semiAxes=f.choice("Chế độ phá tròn",axisList,axes->currentIndex());
    for(const auto& path:paths_)if(path.angle<0&&path.allowance>0){semiAxes->setCurrentIndex(path.axis-4);break;}
    auto* semiItems=qobject_cast<QStandardItemModel*>(semiAxes->model());
    for(int i=0;i<semiAxes->count();++i)semiItems->item(i)->setEnabled(!currentPost(i+4).isEmpty());
    auto *rough=f.choice("Dao phá",toolNames().mid(0,3),rough_);
    auto *finish=f.choice("Dao tinh",toolNames().mid(3),finish_-3);
    auto lengths=tools_;
    auto *l=f.number("L dao phá (mm)",tools_[rough_].overhang,.1,2000);
    auto *fl=f.number("L dao tinh (mm)",tools_[finish_].overhang,.1,2000);
    auto *step=f.number("Bước tinh (mm)",finishStep_,.20,.22);
    auto *allow=f.number("Dư cận tinh (mm)",semiAllowance_,1,1.5);
    auto first=[&](auto predicate,Path fallback){
        const auto found=std::find_if(paths_.cbegin(),paths_.cend(),predicate);
        return found==paths_.cend()?fallback:*found;
    };
    const auto raster=first([](const Path& p){return p.angle>=0;},{"Phá mặt",4,rough_,0,3,2,0});
    const auto semi=first([](const Path& p){return p.angle<0&&p.allowance>0;},{"Phá tròn",axes_,rough_,-1,2.5,1.2,0});
    const auto fine=first([](const Path& p){return p.angle<0&&p.allowance==0;},{"Tinh",axes_,finish_,-1,.22,0,0});
    if(axes->findText(QString::number(fine.axis)+" trục")>=0)axes->setCurrentIndex(fine.axis-4);
    auto* y=f.number("Rộng phôi Y (mm)",std::max(.01,stockY_),.01,10000);
    auto* z=f.number("Cao phôi Z (mm)",std::max(.01,stockZ_),.01,10000);
    y->setEnabled(bool(stock));z->setEnabled(bool(stock));
    auto* roughStep=f.number("Bước dịch phá mặt (mm)",raster.step,.01,100);
    auto* roughDown=f.number("Bước xuống phá mặt (mm)",raster.stepDown>0?raster.stepDown:2,.01,100);
    auto* roughAllowance=f.number("Dư phá mặt (mm)",raster.allowance,0,100);
    auto* semiStep=f.number("Bước dịch phá tròn (mm)",semi.step,.01,100);
    auto* minB=f.number("Góc B Min (°)",fine.bMin,-60,0);
    auto* maxB=f.number("Góc B Max (°)",fine.bMax,0,60);
    auto* safe=f.number("Cách phôi an toàn (mm)",safeClearance_,50,2000);
    auto* direction=f.choice("Hướng chạy X",{"Đáy → Đỉnh","Đỉnh → Đáy"},fine.reverse);
    f.fields->addRow("Lượng dư tinh",new QLabel("0,00 mm"));
    auto *status=new QLabel;status->setWordWrap(true);f.fields->addRow("Hồ sơ đang dùng",status);
    auto* reportLabel=new QLabel;reportLabel->setWordWrap(true);
    reportLabel->setObjectName("camReport");
    f.fields->addRow("Báo cáo quy chuẩn",reportLabel);
    const double initialSizes[]={y->value(),z->value()};
    auto draftStock=[&]{
        Stock draft=stock?*stock:Stock{};
        const double sizes[]={y->value(),z->value()};
        for(int i=1;i<3;++i){
            if(sizes[i-1]==initialSizes[i-1])continue;
            const double center=(draft.min[i]+draft.max[i])*.5;
            const double half=std::max(.005,sizes[i-1]*.5-draft.expansion);
            draft.min[i]=center-half;
            draft.max[i]=center+half;
        }
        return draft;
    };
    auto report=[&]{
        const auto r=stock&&radius?radius(draftStock()):corner_;
        status->setText(QString("Phôi %1 × %2 × %3 mm · R góc %4 mm · Nhấc ≥ %5 mm\nPost: %6")
            .arg(stockX_).arg(y->value()).arg(z->value()).arg(r,0,'f',3).arg(r+safe->value(),0,'f',3)
            .arg(currentPost(4+std::max(axes->currentIndex(),semiAxes->currentIndex()))));
        reportLabel->setText(QString("%1 · dịch %2 mm · xuống %3 mm · dư %4 mm\n"
            "Phá tròn %5 · dịch %6 mm · dư %7 mm · L dao %8 mm\n"
            "Tinh %9 · dịch %10 mm · dư 0 mm · L dao %11 mm\n"
            "B %12° đến %13° · %14 · Nhấc ≥ %15 mm")
            .arg(faces->currentText()).arg(roughStep->value()).arg(roughDown->value()).arg(roughAllowance->value())
            .arg(semiAxes->currentText()).arg(semiStep->value()).arg(allow->value()).arg(l->value())
            .arg(axes->currentText()).arg(step->value()).arg(fl->value())
            .arg(minB->value()).arg(maxB->value()).arg(direction->currentText()).arg(r+safe->value(),0,'f',3));
    };
    for(auto* box:{faces,axes,semiAxes,direction,rough,finish})
        QObject::connect(box,&QComboBox::currentIndexChanged,&f.d,report);
    for(auto* input:{y,z,roughStep,roughDown,roughAllowance,semiStep,minB,maxB,safe,l,fl,step,allow})
        QObject::connect(input,&QDoubleSpinBox::valueChanged,&f.d,report);
    report();
    auto key=[&](QString name){if(!applying)locks.insert(name);};
    const QList<std::pair<const char*,QDoubleSpinBox*>> namedInputs{
        {"stockY",y},{"stockZ",z},{"roughStep",roughStep},{"roughDown",roughDown},
        {"roughAllowance",roughAllowance},{"semiStep",semiStep},{"minB",minB},{"maxB",maxB},{"safe",safe}};
    for(auto [name,box]:namedInputs)
        QObject::connect(box,&QDoubleSpinBox::valueChanged,&f.d,[&,name]{key(name);});
    QObject::connect(direction,&QComboBox::currentIndexChanged,&f.d,[&]{key("direction");});
    QObject::connect(semiAxes,&QComboBox::currentIndexChanged,&f.d,[&]{key("semiAxes");});
    QObject::connect(faces,&QComboBox::currentIndexChanged,&f.d,[&]{key("faces");});
    QObject::connect(axes,&QComboBox::currentIndexChanged,&f.d,[&]{key("axes");report();});
    QObject::connect(rough,&QComboBox::currentIndexChanged,&f.d,[&](int i){key("rough");QSignalBlocker block(l);l->setValue(lengths[i].overhang);});
    QObject::connect(finish,&QComboBox::currentIndexChanged,&f.d,[&](int i){key("finish");QSignalBlocker block(fl);fl->setValue(lengths[i+3].overhang);});
    QObject::connect(l,&QDoubleSpinBox::valueChanged,&f.d,[&](double v){int i=rough->currentIndex();key("length"+QString::number(i));lengths[i].overhang=v;});
    QObject::connect(fl,&QDoubleSpinBox::valueChanged,&f.d,[&](double v){int i=finish->currentIndex()+3;key("length"+QString::number(i));lengths[i].overhang=v;});
    QObject::connect(step,&QDoubleSpinBox::valueChanged,&f.d,[&]{key("step");});
    QObject::connect(allow,&QDoubleSpinBox::valueChanged,&f.d,[&]{key("allow");});
    auto *feedback=new QLabel;feedback->setWordWrap(true);f.fields->addRow(feedback);
    QObject::connect(parse,&QPushButton::clicked,&f.d,[&]{
        const QString s=normalized(prompt->toPlainText());
        auto value=[&](QString re){return QRegularExpression(re).match(s).captured(1);};
        applying=true;QStringList matched;
        auto take=[&](const QString& field,const QString& v){if(v.isEmpty()||locks.contains(field))return false;matched<<field;return true;};
        auto v=value(R"(([24])\s*mat)");if(take("faces",v))faces->setCurrentIndex(v=="2"?0:1);
        v=value(R"(([45])\s*(?:truc|x\b))");if(!currentPost(v.toInt()).isEmpty()&&take("axes",v)){axes->setCurrentIndex(v.toInt()-4);if(!locks.contains("semiAxes"))semiAxes->setCurrentIndex(v.toInt()-4);}
        v=value(R"((?:dao\s*pha\s*(?:[øØd]\s*)?|[øØ]\s*)(12\.7|10|6)\b)");if(take("rough",v))rough->setCurrentIndex(v=="10"?1:v=="6"?2:0);
        v=value(R"((?:goc|con)\s*(10|15)\b)");if(take("finish",v))finish->setCurrentIndex(v=="15"?1:0);
        v=value(R"((?:dao\s*pha[^,;\n]{0,22}?\s+(?:dai|nho)|chieu dai dao pha)\s*([0-9]+(?:\.[0-9]+)?))");
        if(take("length"+QString::number(rough->currentIndex()),v)&&v.toDouble()>0)l->setValue(v.toDouble());
        v=value(R"((?:dao\s*tinh[^,;\n]{0,22}?\s+(?:dai|nho)|chieu dai dao tinh)\s*([0-9]+(?:\.[0-9]+)?))");
        if(take("length"+QString::number(finish->currentIndex()+3),v)&&v.toDouble()>0)fl->setValue(v.toDouble());
        v=value(R"(buoc\s*(?:dich\s*)?(?:tinh\s*)?([0-9]+(?:\.[0-9]+)?))");if(take("step",v)&&v.toDouble()>=.20&&v.toDouble()<=.22)step->setValue(v.toDouble());
        v=value(R"(du\s*(?:can tinh\s*)?([0-9]+(?:\.[0-9]+)?))");if(take("allow",v)&&v.toDouble()>=1&&v.toDouble()<=1.5)allow->setValue(v.toDouble());
        auto applyNumber=[&](const char* name,QDoubleSpinBox* box,const QString& pattern){
            const auto number=value(pattern);
            if(!number.isEmpty()&&box->isEnabled()&&number.toDouble()>=box->minimum()&&
               number.toDouble()<=box->maximum()&&take(name,number))box->setValue(number.toDouble());
        };
        applyNumber("stockY",y,R"((?:phoi y|rong phoi)\s*=?\s*([0-9]+(?:\.[0-9]+)?))");
        applyNumber("stockZ",z,R"((?:phoi z|cao phoi)\s*=?\s*([0-9]+(?:\.[0-9]+)?))");
        applyNumber("roughStep",roughStep,R"(dich pha(?: mat)?\s*([0-9]+(?:\.[0-9]+)?))");
        applyNumber("roughDown",roughDown,R"((?:buoc )?xuong(?: pha)?\s*([0-9]+(?:\.[0-9]+)?))");
        applyNumber("roughAllowance",roughAllowance,R"(du pha mat\s*([0-9]+(?:\.[0-9]+)?))");
        applyNumber("semiStep",semiStep,R"(dich pha tron\s*([0-9]+(?:\.[0-9]+)?))");
        applyNumber("minB",minB,R"(b min\s*=?\s*(-?[0-9]+(?:\.[0-9]+)?))");
        applyNumber("maxB",maxB,R"(b max\s*=?\s*([0-9]+(?:\.[0-9]+)?))");
        applyNumber("safe",safe,R"((?:cach phoi|nhac an toan)\s*([0-9]+(?:\.[0-9]+)?))");
        if(s.contains("dinh ve day")&&take("direction","reverse"))direction->setCurrentIndex(1);
        else if(s.contains("day len dinh")&&take("direction","forward"))direction->setCurrentIndex(0);
        applying=false;feedback->setText(matched.isEmpty()?"Chưa nhận được thông số mới. Các ô đã chỉnh tay được giữ nguyên.":"Đã đọc mô tả. Kiểm tra các ô trước khi lưu.");report();
    });
    AiAction choice=Save;
    if(action){
        auto button=[&](const QString& label,AiAction command,const char* role){
            auto* b=new QPushButton(label);
            b->setProperty("accent",role);
            f.fields->addRow(b);
            QObject::connect(b,&QPushButton::clicked,&f.d,[&,command]{choice=command;f.d.accept();});
        };
        button("🤖 LƯU MÔ TẢ & TỰ ĐỘNG TÍNH TOÀN BỘ ĐƯỜNG DAO NGAY",CalculateAll,"ai");
        button("⬇ Xuất NC Phá (.tap/.nc)",ExportRough,"export");
        button("⬇ Xuất NC Tinh (.tap/.nc)",ExportFinish,"export");
    }
    f.scroll();
    if(!f.accept())return false;
    if(currentPost(axes->currentIndex()+4).isEmpty()||currentPost(semiAxes->currentIndex()+4).isEmpty()){QMessageBox::warning(parent,"Post","Hồ sơ máy thiếu post tương thích.");return false;}
    for(const auto& tool:lengths)if(tool.overhang<tool.flute){
        QMessageBox::warning(parent,"Dao","Chiều nhô phải bằng hoặc lớn hơn chiều dài lưỡi cắt.");return false;
    }
    if(stock&&((y->value()!=initialSizes[0]&&y->value()<=2*stock->expansion)||
               (z->value()!=initialSizes[1]&&z->value()<=2*stock->expansion))){
        QMessageBox::warning(parent,"Phôi","Kích thước phôi phải lớn hơn tổng phần nở ở hai phía.");return false;
    }
    manual_=locks;prompt_=prompt->toPlainText();
    int nextFaces=faces->currentIndex()==0?2:4;
    const bool planChanged=nextFaces!=faces_||rough_!=rough->currentIndex()||finish_!=finish->currentIndex()+3||finishStep_!=step->value()||semiAllowance_!=allow->value();
    setAxes(std::max(axes->currentIndex(),semiAxes->currentIndex())+4,false);
    rough_=rough->currentIndex();finish_=finish->currentIndex()+3;
    semiAllowance_=allow->value();finishStep_=step->value();
    for(int i=0;i<lengths.size();++i)syncLength(i,lengths[i].overhang);
    if(planChanged) {
        auto previous=paths_;
        preset(nextFaces);
        for(auto& path:paths_) {
            auto found=std::find_if(previous.cbegin(),previous.cend(),[&](const Path& old){return old.name==path.name;});
            if(found==previous.cend())continue;
            const auto defaults=path;
            path=*found;
            path.tool=defaults.tool;
            if(!path.manualStep)path.step=defaults.step;
            if(!path.manualAllowance)path.allowance=defaults.allowance;
            path.active=found->active;
            path.manualStep=found->manualStep;
            path.manualAllowance=found->manualAllowance;
            path.revision=found->revision+1;
        }
    }
    safeClearance_=safe->value();
    if(stock)*stock=draftStock();
    for(auto& path:paths_){
        const auto old=path;
        if(path.angle>=0){
            if(!path.manualStep)path.step=roughStep->value();
            if(!path.manualAllowance)path.allowance=roughAllowance->value();
            path.stepDown=roughDown->value();
        }else if(path.allowance>0){
            if(!path.manualStep)path.step=semiStep->value();
            path.axis=semiAxes->currentIndex()+4;
        }else path.axis=axes->currentIndex()+4;
        path.reverse=direction->currentIndex()==1;
        path.bMin=minB->value();path.bMax=maxB->value();
        if(path.step!=old.step||path.allowance!=old.allowance||path.stepDown!=old.stepDown||
           path.axis!=old.axis||path.reverse!=old.reverse||path.bMin!=old.bMin||path.bMax!=old.bMax)
            ++path.revision;
    }
    updateReach();
    if(action)*action=choice;
    return true;
}
bool Workflow::editSurface(QWidget *parent){
    Form f(parent,"Mặt dẫn · B-spline tuần hoàn");auto *cols=f.number("Điểm quanh chu vi",columns_,8,96,0);
    auto *stacks=f.number("Lát theo trục",stacks_,4,128,0);
    if(!f.accept())return false;
    if(columns_!=int(cols->value())||stacks_!=int(stacks->value())){columns_=int(cols->value());stacks_=int(stacks->value());for(auto &p:paths_)if(p.axis==5)++p.revision;}
    return true;
}
bool Workflow::setPathActive(int index,bool active){
    if(index<0||index>=paths_.size()||paths_[index].active==active)return false;
    paths_[index].active=active;return true;
}
bool Workflow::selectPaths(bool finish){
    bool changed=false;
    for(int i=0;i<paths_.size();++i){
        const auto& p=paths_[i];
        changed=setPathActive(i,(p.angle<0&&p.allowance==0)==finish)||changed;
    }
    return changed;
}
bool Workflow::addPath(int source){
    if(paths_.size()>=128||source>=paths_.size())return false;
    Path draft=source<0?Path{"Đường dao mới",axes_,finish_,-1,finishStep_,0,0}:paths_[source];
    if(source>=0)draft.name+=" · Bản sao";
    paths_.append(draft);updateReach();return true;
}
bool Workflow::removePath(int index){
    if(index<0||index>=paths_.size())return false;
    paths_.removeAt(index);return true;
}
bool Workflow::editPath(QWidget *parent,int index,bool* calculate){
    if(index<0||index>=paths_.size())return false;
    auto draft=paths_[index];Form f(parent,"Đường dao · "+draft.name);
    auto *name=new QLineEdit(draft.name);f.fields->addRow("Tên đường dao",name);
    auto *active=new QCheckBox;active->setChecked(draft.active);f.fields->addRow("Đưa vào công việc",active);
    auto *tool=f.choice("Dao",toolNames(),draft.tool);
    auto *strategy=f.choice("Chiến lược",{"3X Raster (phân độ A)","4X đồng thời XYZA","5X đồng thời XYZAB"},draft.angle>=0?0:draft.axis-3);
    auto* items=qobject_cast<QStandardItemModel*>(strategy->model());
    items->item(2)->setEnabled(!currentPost(5).isEmpty());
    const QVector<int> angles{0,90,180,270};
    auto* angle=f.choice("Mặt phân độ",{"0°","90°","180°","270°"},int(std::max<qsizetype>(0,angles.indexOf(int(draft.angle)))));
    auto *step=f.number("Bước dịch (mm)",draft.step,.01,100);
    auto *down=f.number("Bước xuống chia lớp (mm)",draft.stepDown,0,100);
    auto *density=f.number("Khoảng cách điểm (0 = tự động)",draft.pointSpacing,0,100);
    auto *allow=f.number("Lượng dư (mm)",draft.allowance,0,100);
    auto *z0=f.number("Min Z (mm)",draft.zMin,-1e6,1e6);
    auto *z1=f.number("Max Z (mm)",draft.zMax,-1e6,1e6);
    auto *feed=f.number("Tốc độ cắt F (mm/phút)",draft.feed,1,100000);
    auto *rpm=f.number("Tốc độ trục chính (RPM)",draft.rpm,1,60000,0);
    auto *b0=f.number("Góc B Min (°)",draft.bMin,-60,0);
    auto *b1=f.number("Góc B Max (°)",draft.bMax,0,60);
    auto *lead=f.number("Nghiêng theo hướng chạy · Lead (°)",draft.lead,-60,60);
    auto *lean=f.number("Nghiêng ngang · Lean (°)",draft.lean,-60,60);
    lead->setToolTip("Dấu dương nghiêng theo tiếp tuyến đường chạy.");
    lean->setToolTip("Dấu dương nghiêng sang phía pháp tuyến × tiếp tuyến. Máy thực hiện bằng cặp A/B trong giới hạn cho phép.");
    auto *dir=f.choice("Hướng chạy X",{"Đáy → Đỉnh","Đỉnh → Đáy"},draft.reverse);
    auto *lo=f.number("Clipping đầu trục (mm)",draft.clipMin,-1e6,1e6);
    auto *hi=f.number("Clipping cuối trục (mm)",draft.clipMax,-1e6,1e6);
    auto updateStrategy=[&]{
        angle->setEnabled(strategy->currentIndex()==0);
        down->setEnabled(angle->isEnabled());
        const bool simultaneous=strategy->currentIndex()==2;
        for(auto* control:{b0,b1,lead,lean})control->setEnabled(simultaneous);
    };
    QObject::connect(strategy,&QComboBox::currentIndexChanged,&f.d,updateStrategy);updateStrategy();
    f.fields->addRow("Nhấc đầu/cuối tối thiểu",new QLabel(QString::number(corner_+safeClearance_,'f',3)+" mm"));
    bool run=false;
    if(calculate){
        auto* btn=new QPushButton("⚡ Tính riêng đường này");
        btn->setProperty("accent","calculate");
        f.fields->addRow(btn);
        QObject::connect(btn,&QPushButton::clicked,&f.d,[&]{run=true;f.d.accept();});
    }
    f.scroll();
    if(!f.accept())return false;
    if(name->text().trimmed().isEmpty()||lo->value()>=hi->value()||z0->value()>z1->value()){
        QMessageBox::warning(parent,"Đường dao","Cần tên đường dao và các khoảng Z/Clipping hợp lệ.");return false;
    }
    draft.name=name->text().trimmed().left(150);draft.active=active->isChecked();draft.tool=tool->currentIndex();
    draft.angle=strategy->currentIndex()==0?angles[angle->currentIndex()]:-1;
    draft.axis=strategy->currentIndex()==2?5:4;
    draft.manualStep|=draft.step!=step->value();draft.manualAllowance|=draft.allowance!=allow->value();
    draft.step=step->value();draft.allowance=allow->value();
    draft.stepDown=down->value();draft.pointSpacing=density->value();
    draft.zMin=z0->value();draft.zMax=z1->value();draft.feed=feed->value();draft.rpm=int(rpm->value());
    draft.bMin=b0->value();draft.bMax=b1->value();draft.reverse=dir->currentIndex()==1;
    draft.lead=lead->value();draft.lean=lean->value();
    draft.clipMin=lo->value();draft.clipMax=hi->value();
    ++draft.revision;paths_[index]=draft;
    if(draft.axis==5&&axes_==4)axes_=5;
    updateReach();
    if(calculate)*calculate=run;
    return true;
}
QJsonObject Workflow::snapshot()const{
    QJsonArray tools,paths;
    for(const auto&t:tools_)tools.append(QJsonObject{{"name",t.name},{"diameter",t.diameter},{"tipRadius",t.radius},{"singleSideHalfAngle",t.angle},{"overhang",t.overhang},{"outLen",t.outLen},{"flute",t.flute},{"holderDiameter",t.holderDiameter}});
    for(const auto&p:paths_)paths.append(QJsonObject{{"name",p.name},{"axes",p.axis},{"toolIndex",p.tool},{"faceAngle",p.angle},{"step",p.step},{"allowance",p.allowance},{"active",p.active},{"revision",double(p.revision)},{"minZ",p.minZ},{"minimumRetract",corner_+safeClearance_},{"manualStep",p.manualStep},{"manualAllowance",p.manualAllowance},
        {"pointSpacing",p.pointSpacing},{"stepDown",p.stepDown},{"zMin",p.zMin},{"zMax",p.zMax},
        {"clipMin",p.clipMin},{"clipMax",p.clipMax},{"bMin",p.bMin},{"bMax",p.bMax},{"lead",p.lead},{"lean",p.lean},{"feed",p.feed},{"rpm",p.rpm},{"reverse",p.reverse}});
    QJsonArray locks;
    for(const auto& field:manual_)locks.append(field);
    return {{"faces",faces_},{"axes",axes_},{"post",currentPost(axes_)},{"tools",tools},{"paths",paths},
        {"wrapColumns",columns_},{"wrapStacks",stacks_},{"rough",rough_},{"finish",finish_},
        {"safeClearance",safeClearance_},{"semiAllowance",semiAllowance_},{"finishStep",finishStep_},{"manual",locks},{"prompt",prompt_}};
}

bool Workflow::restore(const QJsonObject& state){
    const auto savedTools=state.value("tools").toArray(),savedPaths=state.value("paths").toArray();
    if(savedTools.size()!=tools_.size()||savedPaths.size()>128)return false;
    const int storedAxes=state.value("axes").toInt(),storedFaces=state.value("faces").toInt();
    const int rough=state.value("rough").toInt(-1),finish=state.value("finish").toInt(-1);
    const int cols=state.value("wrapColumns").toInt(),rows=state.value("wrapStacks").toInt();
    const double semi=state.value("semiAllowance").toDouble(),step=state.value("finishStep").toDouble();
    if((storedAxes!=4&&storedAxes!=5)||(storedFaces!=2&&storedFaces!=4)||
       rough<0||rough>2||finish<3||finish>=savedTools.size()||cols<8||cols>96||rows<4||rows>128||
       semi<1||semi>1.5||step<.20||step>.22)return false;
    auto get=[](const QJsonObject& src,const char* key){
        auto v=src.value(key);
        return v.isDouble()?v.toDouble():std::numeric_limits<double>::quiet_NaN();
    };
    QVector<Tool> restoredTools;
    for(auto item:savedTools){
        const auto o=item.toObject();
        Tool t{o.value("name").toString(),get(o,"diameter"),get(o,"tipRadius"),get(o,"singleSideHalfAngle"),
            get(o,"overhang"),get(o,"outLen"),get(o,"flute"),get(o,"holderDiameter")};
        if(t.name.isEmpty()||!(t.diameter>=.01&&t.diameter<=500&&t.radius>=0&&t.radius*2<=t.diameter&&
            t.angle>=0&&t.angle<=89&&t.overhang>=.1&&t.overhang<=2000&&t.outLen==t.overhang&&
            t.flute>=.1&&t.flute<=t.overhang&&t.holderDiameter>=t.diameter&&t.holderDiameter<=500))return false;
        restoredTools.append(t);
    }
    QVector<Path> restoredPaths;
    for(auto item:savedPaths){
        const auto o=item.toObject();
        Path p{o.value("name").toString(),o.value("axes").toInt(),o.value("toolIndex").toInt(-1),
            get(o,"faceAngle"),get(o,"step"),get(o,"allowance"),0,o.value("active").toBool(),1,
            o.value("manualStep").toBool(),o.value("manualAllowance").toBool()};
        if(p.name.isEmpty()||(p.axis!=4&&p.axis!=5)||p.tool<0||p.tool>=restoredTools.size()||
           !(p.angle==-1||p.angle==0||p.angle==90||p.angle==180||p.angle==270)||
           !(p.step>=.01&&p.step<=100&&p.allowance>=0&&p.allowance<=100))return false;
        p.axis=std::min(p.axis,machineAxes_);
        p.pointSpacing=o["pointSpacing"].toDouble();p.stepDown=o["stepDown"].toDouble();
        p.zMin=o["zMin"].toDouble(-1e6);p.zMax=o["zMax"].toDouble(1e6);
        p.clipMin=o["clipMin"].toDouble(-1e6);p.clipMax=o["clipMax"].toDouble(1e6);
        p.bMin=o["bMin"].toDouble(-20);p.bMax=o["bMax"].toDouble(20);
        p.lead=o["lead"].toDouble();p.lean=o["lean"].toDouble();
        if(std::abs(p.lead)>60||std::abs(p.lean)>60)return false;
        p.feed=o["feed"].toDouble(600);p.rpm=o["rpm"].toInt(16000);p.reverse=o["reverse"].toBool();
        restoredPaths.append(p);
    }
    tools_=std::move(restoredTools);paths_=std::move(restoredPaths);
    axes_=std::min(storedAxes,machineAxes_);faces_=storedFaces;rough_=rough;finish_=finish;
    if(currentPost(axes_).isEmpty()&&!currentPost(9-axes_).isEmpty())axes_=9-axes_;
    if(axes_==4)syncPathAxes();
    safeClearance_=std::clamp(state.value("safeClearance").toDouble(50),50.,2000.);
    columns_=cols;stacks_=rows;semiAllowance_=semi;finishStep_=step;prompt_=state.value("prompt").toString();
    manual_.clear();
    for(auto lock:state.value("manual").toArray())if(lock.isString())manual_.insert(lock.toString());
    updateReach();return true;
}
