#include <QtWidgets>
#include <QOpenGLContext>
#include <QJsonArray>
#include <QProcess>
#include <QScopeGuard>
#include <QJsonDocument>
#include <cmath>
#include <algorithm>
#include <utility>
#include <QStandardPaths>
#include <QRegularExpression>
#include <QDir>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "view.h"
#include "profiles.h"
#include "workflow.h"
#include "stock.h"
#include "theme.h"

#ifndef ARTISAN_VERSION
#define ARTISAN_VERSION "1.1.0"
#endif

class Workshop final : public QMainWindow {
    friend struct WorkshopAudit;
    View* view;
    QTreeWidget* tree=new QTreeWidget;
    QLabel* summary=new QLabel;
    MachineProfiles machine{this};
    Workflow flow;
    QComboBox* modeBox=new QComboBox;
    QAction* cancelJob=nullptr;
    QProgressBar* progressBar=new QProgressBar;
    QLabel* progressText=new QLabel("Sẵn sàng");
    QElapsedTimer jobClock;
    QTimer progressTimer,playTimer;
    QByteArray progressPending;
    QString progressStage;
    QWidget* projectControls=nullptr;
    QSlider* scrub=new QSlider(Qt::Horizontal);
    QPushButton* playbackButton=new QPushButton("▶ Mô phỏng");
    QComboBox* playbackRate=new QComboBox;
    QLabel* timing=new QLabel("Chưa tính");
    QString ncPreview,verifiedFile,jobNc,exportTarget,jobInput;
    QTemporaryDir spool;
    QSet<int> calculatedPaths,jobPaths,lastNcPaths;
    QJsonArray playback;
    double playhead=0;
    bool stockAuto=true;
    QSet<QAction*> jobControls;
    QList<QAction*> suspendedControls;
    Stock stock;
    QString modelFile,guideFile;
    QJsonArray stages,diagnostics,lifts;
    QComboBox* stagesBox=new QComboBox;
    double voxelPitch=0;
    QVector<QMatrix4x4> history;
    QProcess worker;
    QByteArray pendingJob, nc;
    std::vector<float> cutOverlay,surfaceOverlay;
    bool saveWorkspaceEnabled=false;
    static QString workspacePath(){
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/workspace-v1.json";
    }
    QJsonArray modelTransform()const{
        QJsonArray values;const auto matrix=view->modelTransform();
        for(int i=0;i<16;++i)values.append(matrix.constData()[i]);
        return values;
    }
    QJsonObject stockJson()const{
        QJsonArray lo,hi;for(int i=0;i<3;++i){lo.append(stock.min[i]);hi.append(stock.max[i]);}
        return {{"shape",stock.shape},{"min",lo},{"max",hi},{"expansion",stock.expansion},{"endRatio",stock.endRatio}};
    }
    void saveWorkspace()const{
        if(!saveWorkspaceEnabled)return;
        const auto path=workspacePath();QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile out(path);
        const auto data=QJsonDocument(QJsonObject{{"version",1},{"file",modelFile},{"guideFile",guideFile},
            {"transform",modelTransform()},{"stockAuto",stockAuto},{"stock",stockJson()},{"workflow",flow.snapshot()}}).toJson(QJsonDocument::Compact);
        if(!out.open(QIODevice::WriteOnly)||out.write(data)!=data.size()||!out.commit())
            qWarning()<<"Failed to save workspace"<<out.errorString();
    }
    void restoreWorkspace(){
        QFile file(workspacePath());
        if(!file.open(QIODevice::ReadOnly)||file.size()>1048576)return;
        auto values=QJsonDocument::fromJson(file.readAll()).object();if(values.value("version").toInt()!=1)return;
        flow.restore(values.value("workflow").toObject());
        auto item=values.value("stock").toObject();
        auto lo=item.value("min").toArray(),hi=item.value("max").toArray();
        Stock restored;restored.shape=item.value("shape").toString();
        restored.expansion=item.value("expansion").toDouble(-1);restored.endRatio=item.value("endRatio").toDouble(-1);
        bool valid=lo.size()==3&&hi.size()==3&&(restored.shape=="box"||restored.shape=="cylinder"||restored.shape=="frustum")&&
            restored.expansion>=0&&restored.expansion<=100000&&restored.endRatio>=0&&restored.endRatio<=1;
        for(int i=0;valid&&i<3;++i){
            restored.min[i]=lo[i].toDouble(1e9);restored.max[i]=hi[i].toDouble(-1e9);
            valid=std::abs(restored.min[i])<=1e6&&std::abs(restored.max[i])<=1e6&&restored.min[i]<restored.max[i];
        }
        if(valid)stock=restored;
        stockAuto=values.value("stockAuto").toBool(false);
        syncStock();refresh();
        const auto path=values.value("file").toString();
        const auto matrix=values.value("transform").toArray();
        if(path.isEmpty()||matrix.size()!=16||!QFileInfo::exists(path))return;
        float transposed[16];
        for(int i=0;i<16;++i){const auto n=matrix[i].toDouble(1e12);if(std::abs(n)>1e10)return;transposed[(i%4)*4+i/4]=float(n);}
        const auto candidateGuide=values.value("guideFile").toString();
        loading=true;const auto request=++loadRequest;
        view->loadSTLAsync(path,[this,path,request,transform=QMatrix4x4(transposed),candidateGuide](bool ok,QString){
            if(request!=loadRequest)return;
            loading=false;
            if(!ok){statusBar()->showMessage("Không khôi phục được STL của phiên trước.");return;}
            modelFile=path;
            guideFile=QFileInfo::exists(candidateGuide)?candidateGuide:QString();
            view->setModelTransform(transform);view->fit();refresh();
            statusBar()->showMessage("Đã khôi phục dự án và cấu hình của phiên trước.");
        });
    }
    void invalidateOutput(){
        ++revision;nc.clear();
        if(!verifiedFile.isEmpty())QFile::remove(std::exchange(verifiedFile,{}));
        ncPreview.clear();calculatedPaths.clear();lastNcPaths.clear();playback={};playhead=0;
        stopPlayback();scrub->setValue(0);updatePlayback();
        cutOverlay.clear();
        view->setOverlay({},surfaceOverlay);view->setSimStock({},{},{});stages={};diagnostics={};lifts={};stagesBox->clear();refresh();saveWorkspace();
    }
    static std::vector<float> unpack(const QJsonValue& values){
        auto a=values.toArray();
        std::vector<float> xyz; xyz.reserve(size_t(a.size())*3);
        for(auto row:a){auto p=row.toArray();if(p.size()!=3)continue;
            for(auto v:p)xyz.push_back(float(v.toDouble()));
        }
        return xyz;
    }
    bool updating=false;
    bool powerHeld=false;
    bool jobBusy=false;
    bool jobCancelled=false;
    void setJobBusy(bool busy){
        jobBusy=busy;
        if(busy){
            for(auto* control:jobControls){
                if(!control->isEnabled())continue;
                suspendedControls.append(control);
                control->setEnabled(false);
            }
        }else{
            for(auto* control:suspendedControls)control->setEnabled(true);
            suspendedControls.clear();
        }
        tree->setEnabled(!busy);
        if(projectControls)projectControls->setEnabled(!busy);
        cancelJob->setEnabled(busy);
        keepAwake(busy);
    }
    void keepAwake(bool active){
#ifdef Q_OS_WIN
        if(active==powerHeld)return;
        if(SetThreadExecutionState(active?(ES_CONTINUOUS|ES_SYSTEM_REQUIRED):ES_CONTINUOUS))
            powerHeld=active;
        else if(active)qWarning("Windows could not prevent idle sleep for CAMWorker");
#else
        Q_UNUSED(active);
#endif
    }
    quint64 revision=0, jobRevision=0, loadRequest=0;
    bool loading=false;
    bool alongY() const {return std::abs(machine.stockAxisDirection().y())>0.5;}
    double radius(const Stock& s) const {return s.radius(machine.rotaryOrigin(),machine.rotaryDirection(),alongY());}
    void drawStock(const Stock& s){
        view->setStock(s.shape,s.low(),s.high(),s.endRatio,alongY());
        const auto extent=[&s](int i){return s.max[i]-s.min[i]+2*s.expansion;};
        const double r=radius(s);
        summary->setText(QString("Phôi %1 × %2 × %3 mm    |    R góc %4 mm    |    Nhấc ≥ %5 mm")
            .arg(extent(0),0,'f',4).arg(extent(1),0,'f',4).arg(extent(2),0,'f',4).arg(r,0,'f',3).arg(r+flow.safeClearance(),0,'f',3));
    }
    void syncStock(){
        auto extent=[&](int i){return stock.max[i]-stock.min[i]+2*stock.expansion;};
        double r=radius(stock);flow.setStock(extent(0),extent(1),extent(2),r,r+flow.safeClearance());drawStock(stock);
    }
    void syncMachine(){flow.setMachine(machine.axisCount(),[this](int axes){return machine.postForAxes(axes);});syncStock();}
    bool fitStock(Stock& target)const{
        QVector3D lo,hi;
        if(!view->modelBounds(lo,hi))return false;
        for(int i=0;i<3;++i){
            target.min[i]=lo[i];target.max[i]=std::max(hi[i],lo[i]+.0001f);
        }
        target.shape="box";
        return true;
    }
    void modelChanged(){
        if(stockAuto&&fitStock(stock))syncStock();
        surfaceOverlay.clear();guideFile.clear();
        changed();
    }
    void changed(){flow.invalidateAll();invalidateOutput();}
    void stopPlayback(){playTimer.stop();playbackButton->setText("▶ Mô phỏng");}
    void updatePlayback(){
        const bool ready=playback.size()>1;
        playbackButton->setEnabled(ready);scrub->setEnabled(ready);playbackRate->setEnabled(ready);
        if(!ready){view->clearPlayback();timing->setText("Chưa tính");return;}
        const auto last=playback.last().toArray();
        const double total=last[0].toDouble();
        const auto next=std::lower_bound(playback.begin(),playback.end(),playhead,[](const QJsonValue& frame,double time){
            return frame.toArray()[0].toDouble()<time;
        });
        const auto hi=(next==playback.end()?last:next->toArray());
        const auto lo=next==playback.begin()?hi:(next-1)->toArray();
        const auto dt=hi[0].toDouble()-lo[0].toDouble();
        const auto mix=dt>0?std::clamp((playhead-lo[0].toDouble())/dt,0.,1.):0.;
        auto get=[&](int i){return lo[i].toDouble()+(hi[i].toDouble()-lo[i].toDouble())*mix;};
        auto tools=flow.snapshot()["tools"].toArray();
        const int tool=lo[6].toInt(-1);
        const auto spec=tool>=0&&tool<tools.size()?tools[tool].toObject():QJsonObject{};
        view->setPlayback({float(get(1)),float(get(2)),float(get(3))},get(4),get(5),alongY(),machine.rotaryOrigin(),
                          spec["diameter"].toDouble(2),spec["overhang"].toDouble(35),spec["holderDiameter"].toDouble(20),
                          spec["tipRadius"].toDouble(),spec["singleSideHalfAngle"].toDouble(),spec["flute"].toDouble(12));
        QSignalBlocker blocker(scrub);
        scrub->setValue(total>0?int(1000*playhead/total):0);
        timing->setText(QString("%1% · Ước tính %2 phút").arg(scrub->value()/10).arg(total/60,0,'f',1));
    }
    void showNcPreview(){
        QDialog dlg(this);dlg.setWindowTitle("Xem trước G-Code");dlg.resize(720,520);
        auto* layout=new QVBoxLayout(&dlg);
        auto* editor=new QPlainTextEdit(ncPreview.isEmpty()?"Chưa có chương trình NC để xem trước.":ncPreview);
        editor->setReadOnly(true);editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        layout->addWidget(editor);dlg.exec();
    }
    void refreshProgress(){
        if(!jobBusy)return;
        progressText->setText(QString("%1 · %2 giây").arg(progressStage).arg(jobClock.elapsed()/1000.0,0,'f',1));
    }
    void drainProgress(){
        progressPending+=worker.readAllStandardError();
        for(qsizetype end;(end=progressPending.indexOf('\n'))>=0;){
            const auto record=QJsonDocument::fromJson(progressPending.left(end)).object();
            progressPending.remove(0,end+1);
            if(!record.contains("percent"))continue;
            progressBar->setValue(std::max(progressBar->value(),record["percent"].toInt()));
            progressStage=record["stage"].toString();
            if(record["total"].toInteger()>0)
                progressStage+=QString(" · vòng %1/%2").arg(record["turn"].toInteger()).arg(record["total"].toInteger());
            refreshProgress();
        }
    }
    void refresh(){
        const QSignalBlocker block(modeBox);
        auto* items=qobject_cast<QStandardItemModel*>(modeBox->model());
        for(int i=0;i<modeBox->count();++i){
            const auto post=flow.currentPost(modeBox->itemData(i).toInt());
            items->item(i)->setEnabled(!post.isEmpty());
            items->item(i)->setToolTip(post.isEmpty()?"Hồ sơ máy đang dùng thiếu post cho chế độ này.":"Post: "+post);
        }
        modeBox->setCurrentIndex(modeBox->findData(flow.axes()));
        modeBox->setToolTip(modeBox->currentData(Qt::ToolTipRole).toString());
        const QSignalBlocker treeBlock(tree);
        tree->clear();
        auto branch=[&](QString label,QString key,QStringList children){
            auto* root=new QTreeWidgetItem(tree,{label});root->setData(0,Qt::UserRole,key);
            for(int i=0;i<children.size();++i){auto* child=new QTreeWidgetItem(root,{children[i]});child->setData(0,Qt::UserRole,key);child->setData(0,Qt::UserRole+1,i);}
            root->setExpanded(true);
        };
        branch("Mô hình","model",{modelFile.isEmpty()?QString("Chưa nạp STL"):QFileInfo(modelFile).fileName()});
        branch("Phôi","stock",{stock.shape=="box"?"Hộp":stock.shape=="cylinder"?"Trụ":"Chóp cụt"});
        branch("Thư viện dao","tools",flow.toolNames());branch("Mặt dẫn","surface",{});
        branch("Đường dao","paths",flow.pathNames());
        auto* pathRoot=tree->topLevelItem(tree->topLevelItemCount()-1);
        pathRoot->setData(0,Qt::UserRole,QString{});
        const auto paths=flow.snapshot()["paths"].toArray();
        for(int i=0;i<pathRoot->childCount();++i){
            auto* row=pathRoot->child(i);
            row->setFlags(row->flags()|Qt::ItemIsUserCheckable);
            row->setCheckState(0,paths[i].toObject()["active"].toBool()?Qt::Checked:Qt::Unchecked);
            row->setText(1,calculatedPaths.contains(i)?"Đã tính":"Chưa tính");
        }
        branch("Máy và post","machine",{machine.name()});
        branch("Trợ lý CAM","ai",{});
    }
    void load(){
        if(jobBusy)return;
        QString f=QFileDialog::getOpenFileName(this,"Mở mô hình STL",{},"STL (*.stl *.STL)");
        if(f.isEmpty())return;
        loading=true;const auto request=++loadRequest;
        statusBar()->showMessage("Đang đọc lưới STL…");
        view->loadSTLAsync(f,[this,f,request](bool ok,QString error){
            if(request!=loadRequest)return;
            loading=false;
            if(!ok){QMessageBox::warning(this,"Đọc STL",error);return;}
            modelFile=f;guideFile.clear();history.clear();view->setModelTransform(QMatrix4x4());stockAuto=true;modelChanged();view->fit();
            statusBar()->showMessage(QString("Đã nạp %1 tam giác").arg(view->triangleCount()));
        });
    }
    void transform(const QMatrix4x4& m){if(modelFile.isEmpty())return;history.push_back(view->modelTransform());view->setModelTransform(m*history.last());modelChanged();}
    void base(){QVector3D lo,hi;if(!view->modelBounds(lo,hi))return;QMatrix4x4 m;m.translate(-lo.x(),-(lo.y()+hi.y())/2,-(lo.z()+hi.z())/2);transform(m);}
    void model(){
        if(modelFile.isEmpty()){load();return;}
        QDialog dlg(this);dlg.setWindowTitle("Biến đổi mô hình");QFormLayout layout(&dlg);
        QComboBox axis;axis.addItems({"X","Y","Z"});QDoubleSpinBox degrees;degrees.setRange(-360,360);
        layout.addRow("Xoay quanh trục",&axis);layout.addRow("Góc (độ)",&degrees);
        QDoubleSpinBox scale[3],translation[3];QCheckBox mirror[3];
        for(int i=0;i<3;++i){
            const auto letter=QString("XYZ").mid(i,1);
            scale[i].setDecimals(4);scale[i].setRange(.0001,10000);scale[i].setValue(1);
            translation[i].setDecimals(4);translation[i].setRange(-100000,100000);
            layout.addRow("Tỷ lệ "+letter,&scale[i]);
            layout.addRow("Dịch "+letter+" (mm)",&translation[i]);
            layout.addRow("Đối xứng "+letter,&mirror[i]);
        }
        QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout.addRow(&buttons);
        connect(&buttons,&QDialogButtonBox::accepted,&dlg,&QDialog::accept);connect(&buttons,&QDialogButtonBox::rejected,&dlg,&QDialog::reject);
        if(dlg.exec()==QDialog::Accepted){
            QMatrix4x4 m;QVector3D a(0,0,0);a[axis.currentIndex()]=1;
            m.translate(translation[0].value(),translation[1].value(),translation[2].value());
            m.rotate(degrees.value(),a);
            auto signedScale=[&](int i){return scale[i].value()*(mirror[i].isChecked()?-1:1);};
            m.scale(signedScale(0),signedScale(1),signedScale(2));transform(m);
        }
    }
    void dialog(QString key,int index=0){
        if(jobBusy)return;
        if(key=="model"){model();return;}
        if(key=="stock"){
            view->setSimStock({},{},{});stagesBox->setCurrentIndex(0);
            const auto original=stock;
            if(editStock(this,stock,alongY(),machine.rotaryOrigin(),machine.rotaryDirection(),[this](const Stock& s){drawStock(s);},[this](Stock& s){return fitStock(s);},flow.safeClearance())){
                stockAuto=false;syncStock();changed();
            }else drawStock(original);
        }else if(key=="machine"){if(machine.edit(this)){syncMachine();changed();}}
        else {
            bool saved=false;
            if(key=="tools")saved=flow.editTools(this);
            if(key=="paths"){
                bool calculate=false;
                if(flow.editPath(this,index,&calculate)){
                    invalidateOutput();
                    if(calculate)launchJob("plan-motion",modelFile,{},index);
                }
                return;
            }
            if(key=="surface")saved=flow.editSurface(this);
            if(key=="ai"){
                const auto beforeStock=stockJson(),beforePlan=flow.snapshot();
                Workflow::AiAction cmd;
                if(!flow.editAI(this,&stock,[this](const Stock& draft){return radius(draft);},&cmd))return;
                if(beforeStock!=stockJson()){stockAuto=false;syncStock();}
                else drawStock(stock);
                if(beforeStock!=stockJson()||beforePlan!=flow.snapshot())invalidateOutput();
                if(cmd==Workflow::CalculateAll)launchJob("plan-motion",modelFile,{},-2);
                if(cmd==Workflow::ExportRough||cmd==Workflow::ExportFinish){
                    const auto file=QFileDialog::getSaveFileName(this,"Tính và xuất NC đối chiếu",
                        cmd==Workflow::ExportRough?"Pha.tap":"Tinh.tap","G-Code (*.tap *.nc)");
                    if(!file.isEmpty())launchJob("plan-motion",modelFile,{},cmd==Workflow::ExportRough?-3:-4,file);
                }
                return;
            }
            if(saved)invalidateOutput();
        }
    }
    void launchJob(QString operation,QString file,QString extra={},int scope=-1,QString directedExport={}){
        if(jobBusy||updating)return;
        if(loading||file.isEmpty()){statusBar()->showMessage("Hãy nạp xong một STL trước khi tính.");return;}
        auto plan=flow.snapshot();
        jobPaths.clear();
        if(operation=="plan-motion"){
            QJsonArray selected;
            const auto all=plan["paths"].toArray();
            int axes=4;
            for(int i=0;i<all.size();++i){
                auto path=all[i].toObject();
                const bool fine=path["faceAngle"].toInt()<0&&path["allowance"].toDouble()==0.;
                bool enabled=path["active"].toBool();
                if(scope>=0)enabled=i==scope;
                else if(scope==-2)enabled=true;
                else if(scope<=-3)enabled=(scope==-4)==fine;
                if(enabled){axes=std::max(axes,path["axes"].toInt());jobPaths.insert(i);}
                path["active"]=enabled;
                selected.append(path);
            }
            if(jobPaths.isEmpty()){statusBar()->showMessage("Hãy tích đường dao cần tính.");return;}
            if(flow.currentPost(axes).isEmpty()&&axes==4&&flow.axes()==5)axes=5;
            if(flow.currentPost(axes).isEmpty()){statusBar()->showMessage("Hãy chọn post tương thích trong Hồ sơ máy.");return;}
            plan["paths"]=selected;plan["axes"]=axes;plan["post"]=flow.currentPost(axes);
        }
        exportTarget=std::move(directedExport);jobInput=file;
        jobNc.clear();
        if(operation=="plan-motion"&&spool.isValid())
            jobNc=spool.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces)+".nc");
        pendingJob=QJsonDocument(QJsonObject{{"operation",operation},{"file",file},
            {"machine",machine.current()},{"guideFile",guideFile},{"ncFile",extra},{"ncOutput",jobNc},{"stock",stockJson()},{"workflow",plan},{"retract",radius(stock)+flow.safeClearance()},{"transformColumnMajor",modelTransform()}}).toJson(QJsonDocument::Compact)+"\n";
        stopPlayback();
        progressPending.clear();progressBar->setValue(0);jobClock.restart();
        progressStage="Đang khởi động CAMWorker";
        progressTimer.start(200);
        jobRevision=revision;QString program=QCoreApplication::applicationDirPath()+"/CAMWorker";
#ifdef Q_OS_WIN
        program+=".exe";
#endif
        jobCancelled=false;
        worker.setProgram(program);
        setJobBusy(true);
        worker.start();
    }
    bool saveNc(const QString& destination){
        QSaveFile out(destination);
        if(!out.open(QIODevice::WriteOnly))return false;
        if(!verifiedFile.isEmpty()){
            QFile in(verifiedFile);
            if(!in.open(QIODevice::ReadOnly))return false;
            while(!in.atEnd()){
                const auto chunk=in.read(1024*1024);
                if(chunk.isEmpty()||out.write(chunk)!=chunk.size())return false;
            }
        }else if(nc.isEmpty()||out.write(nc)!=nc.size())return false;
        return out.commit();
    }
    void exportNc(){
        if(jobBusy||loading||updating)return;
        QSet<int> selected;
        const auto paths=flow.snapshot()["paths"].toArray();
        for(int i=0;i<paths.size();++i)if(paths[i].toObject()["active"].toBool())selected.insert(i);
        if(!lastNcPaths.isEmpty()&&selected!=lastNcPaths){
            statusBar()->showMessage("Các đường đang tích đã đổi. Bấm Tính trước khi xuất nhóm này.");return;
        }
        if(nc.isEmpty()&&verifiedFile.isEmpty()){statusBar()->showMessage("Chưa có NC đã qua kiểm tra mô hình.");return;}
        auto reply=QMessageBox::warning(this,"Xuất NC đối chiếu",
            "Tệp đã qua bộ đọc theo mô hình kế hoạch. Cần đối chiếu tọa độ, post, giới hạn và đồ gá trên máy.",
            QMessageBox::Save|QMessageBox::Cancel,QMessageBox::Cancel);
        if(reply!=QMessageBox::Save)return;
        const auto file=QFileDialog::getSaveFileName(this,"Lưu NC đối chiếu","ArtisanAxis_REVIEW.nc","G-Code (*.tap *.nc)");
        if(file.isEmpty())return;
        if(!saveNc(file)){QMessageBox::warning(this,"Lưu NC","Không ghi được tệp đã chọn.");return;}
        launchJob("verify-nc",modelFile,file);
    }
public:
    Workshop(){
        setWindowTitle("ArtisanAxis CAM Pro · Xuân Diệu CNC · v" ARTISAN_VERSION);
        resize(1390,850);view=new View;
        auto* center=new QWidget;
        auto* layout=new QVBoxLayout(center);
        layout->setContentsMargins(8,7,8,8);
        auto* progressRow=new QHBoxLayout;
        layout->addLayout(progressRow);
        progressBar->setRange(0,100);progressBar->setValue(0);progressBar->setFixedWidth(180);
        progressRow->addWidget(progressBar);progressRow->addWidget(progressText,1);
        auto* cancelButton=new QToolButton;
        cancelButton->setProperty("accent","cancel");
        progressRow->addWidget(cancelButton);
        layout->addWidget(view,1);
        auto* playbackRow=new QHBoxLayout;layout->addLayout(playbackRow);
        auto* code=new QPushButton("Xem G-Code");
        scrub->setRange(0,1000);
        for(int rate:{1,2,4,8,12,20})playbackRate->addItem(QString("%1×").arg(rate),rate);
        playbackRow->addWidget(playbackButton);playbackRow->addWidget(scrub,1);
        playbackRow->addWidget(playbackRate);playbackRow->addWidget(timing);playbackRow->addWidget(code);
        connect(code,&QPushButton::clicked,this,[this]{showNcPreview();});
        connect(&progressTimer,&QTimer::timeout,this,[this]{refreshProgress();});
        connect(playbackButton,&QPushButton::clicked,this,[this]{
            if(playTimer.isActive()){stopPlayback();return;}
            if(playback.size()<2)return;
            if(scrub->value()==1000)playhead=0;
            playbackButton->setText("⏸ Dừng");playTimer.start(33);
        });
        connect(&playTimer,&QTimer::timeout,this,[this]{
            const double end=playback.last().toArray()[0].toDouble();
            playhead=std::min(end,playhead+.033*playbackRate->currentData().toInt());
            if(playhead>=end)stopPlayback();
            updatePlayback();
        });
        connect(scrub,&QSlider::valueChanged,this,[this](int position){
            if(playback.isEmpty())return;
            playhead=position/1000.*playback.last().toArray()[0].toDouble();updatePlayback();
        });
        setCentralWidget(center);updatePlayback();
        auto* dock=new QDockWidget("Cây dự án",this);
        auto* panel=new QWidget;
        auto* sidebar=new QVBoxLayout(panel);
        sidebar->setContentsMargins(6,6,6,6);
        projectControls=new QWidget;
        auto* shortcuts=new QGridLayout(projectControls);
        shortcuts->setContentsMargins(0,0,0,0);
        sidebar->addWidget(projectControls);
        tree->setHeaderLabels({"Hạng mục","Trạng thái"});
        tree->setColumnWidth(0,192);
        sidebar->addWidget(tree,1);
        dock->setWidget(panel);addDockWidget(Qt::LeftDockWidgetArea,dock);
        auto quick=[&](QString title,int row,int col,auto run){
            auto* button=new QPushButton(title);
            shortcuts->addWidget(button,row,col);
            connect(button,&QPushButton::clicked,this,run);
        };
        quick("Bộ mẫu 4 Mặt + Tinh",0,0,[this]{flow.preset(4);invalidateOutput();});
        quick("Bộ mẫu 2 Mặt + Tinh",0,1,[this]{flow.preset(2);invalidateOutput();});
        quick("☑ Chỉ tích Phá",1,0,[this]{if(flow.selectPaths(false)){refresh();saveWorkspace();}});
        quick("☑ Chỉ tích Tinh",1,1,[this]{if(flow.selectPaths(true)){refresh();saveWorkspace();}});
        auto selectedPath=[this]{
            const auto* item=tree->currentItem();
            return item&&item->data(0,Qt::UserRole)=="paths"?item->data(0,Qt::UserRole+1).toInt():-1;
        };
        quick("+ Tạo",2,0,[this]{if(flow.addPath()){invalidateOutput();dialog("paths",flow.pathNames().size()-1);}});
        quick("📋 Copy",2,1,[this,selectedPath]{const int i=selectedPath();if(i>=0&&flow.addPath(i))invalidateOutput();});
        quick("🗑 Xóa",3,0,[this,selectedPath]{
            const auto i=selectedPath();
            if(i>=0&&QMessageBox::question(this,"Xóa đường dao","Xóa đường dao đang chọn?")==QMessageBox::Yes&&flow.removePath(i))
                invalidateOutput();
        });
        connect(tree,&QTreeWidget::itemChanged,this,[this](QTreeWidgetItem* item,int column){
            if(jobBusy||column!=0||item->data(0,Qt::UserRole)!="paths")return;
            if(flow.setPathActive(item->data(0,Qt::UserRole+1).toInt(),item->checkState(0)==Qt::Checked))
                saveWorkspace();
        });
        auto* bar=addToolBar("Xưởng");bar->setMovable(false);
        auto action=[&](QString name,auto fn){auto* a=bar->addAction(name);connect(a,&QAction::triggered,this,fn);return a;};
        action("Mở STL",[this]{load();})->setShortcut(QKeySequence::Open);
        for(const QString name:{"Fit","XY","YZ","ZX","3D Iso"})
            action(name,[this,name]{if(name=="Fit")view->fit();else view->setCamera(name=="3D Iso"?"iso":name);});
        bar->addSeparator();
        auto accent=[bar](QAction* act,const char* kind){bar->widgetForAction(act)->setProperty("accent",kind);};
        auto* modeLabel=new QLabel("Chế độ:");
        modeLabel->setBuddy(modeBox);
        bar->addWidget(modeLabel);
        modeBox->setAccessibleName("Chế độ gia công");
        modeBox->addItem("4 Trục đồng thời (XYZA)",4);
        modeBox->addItem("5 Trục đồng thời (XYZAB)",5);
        modeBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        bar->addWidget(modeBox);
        connect(modeBox,&QComboBox::currentIndexChanged,this,[this]{
            int axes=modeBox->currentData().toInt();
            if(axes==flow.axes())return;
            if(jobBusy||!flow.setAxes(axes,true)){refresh();return;}
            invalidateOutput();
            statusBar()->showMessage("Đã chọn "+modeBox->currentText()+" · Post: "+flow.currentPost(axes));
        });
        accent(action("🤖 AI Tự Động Lập Trình (1-Click)",[this]{dialog("ai");}),"ai");
        accent(action("⚡ Tính các đường đã tích",[this]{launchJob("plan-motion",modelFile);}),"calculate");
        accent(action("⬇ Xuất G-Code (.tap/.nc)",[this]{exportNc();}),"export");
        cancelJob=new QAction("Hủy tác vụ",this);
        cancelButton->setDefaultAction(cancelJob);
        connect(cancelJob,&QAction::triggered,this,[this]{
            if(!jobBusy||jobCancelled)return;
            jobCancelled=true;
            cancelJob->setEnabled(false);
            progressStage="Đang hủy tác vụ…";
            statusBar()->showMessage(progressStage);
            worker.kill();
        });
        cancelJob->setEnabled(false);
        addToolBarBreak();
        auto* cameras=addToolBar("Cấu hình và khung nhìn");
        cameras->setMovable(false);
        auto secondary=[&](QString name,auto fn){
            auto* a=cameras->addAction(name);connect(a,&QAction::triggered,this,fn);return a;
        };
        secondary("Biến đổi",[this]{model();});secondary("Lấy gốc X0",[this]{base();});
        secondary("Hoàn tác",[this]{if(!history.isEmpty()){view->setModelTransform(history.takeLast());modelChanged();}})->setShortcut(QKeySequence::Undo);
        secondary("Phôi",[this]{dialog("stock");});secondary("Dao",[this]{dialog("tools");});secondary("Máy / Post",[this]{dialog("machine");});
        secondary("Trợ lý",[this]{dialog("ai");});secondary("2 mặt + tinh",[this]{flow.preset(2);invalidateOutput();});
        secondary("4 mặt + tinh",[this]{flow.preset(4);invalidateOutput();});
        secondary("Xuất NC",[this]{exportNc();});
        secondary("Đọc lại NC",[this]{
            auto file=QFileDialog::getOpenFileName(this,"Đọc NC xuất từ Mốc 3",{},"NC (*.nc *.tap)");
            if(!file.isEmpty())launchJob("verify-nc",modelFile,file);
        });
        for(QString key:{"iso","xy","yz","zx"})connect(cameras->addAction(key.toUpper()),&QAction::triggered,this,[this,key]{view->setCamera(key);});
        connect(cameras->addAction("Vừa khung"),&QAction::triggered,this,[this]{view->fit();});
        auto* showStock=cameras->addAction("Hiện phôi");showStock->setCheckable(true);showStock->setChecked(true);
        connect(showStock,&QAction::toggled,view,&View::setStockVisible);
        auto* showModel=cameras->addAction("Hiện STL");showModel->setCheckable(true);showModel->setChecked(true);
        connect(showModel,&QAction::toggled,view,&View::setModelVisible);
        cameras->addWidget(stagesBox);
        connect(stagesBox,&QComboBox::currentIndexChanged,this,[this](int index){
            if(index<1){view->setSimStock({},{},{});return;}
            const auto stage=stages.at(index-1).toObject();
            view->setSimStock(unpack(stage.value("shell")),unpack(stage.value("excessMap")),unpack(stage.value("gougeMap")),voxelPitch);
            statusBar()->showMessage(QString("Ô %1 mm · Còn %2 · Dư (cam) %3 · Lẹm (đỏ) %4")
                .arg(voxelPitch,0,'f',3).arg(stage.value("remaining").toInteger())
                .arg(stage.value("excess").toInteger()).arg(stage.value("gouge").toInteger()));
        });
        connect(cameras->addAction("Nhật ký NC"),&QAction::triggered,this,[this]{
            QDialog dlg(this);dlg.setWindowTitle("Kiểm chứng NC và điểm tách vùng");dlg.resize(690,440);
            auto* layout=new QVBoxLayout(&dlg);auto* report=new QPlainTextEdit;report->setReadOnly(true);
            QStringList rows;
            for(auto value:diagnostics){auto row=value.toObject();rows<<QString("Dòng %1 · %2").arg(row.value("line").toInt()).arg(row.value("message").toString());}
            for(auto value:lifts){
                auto row=value.toObject();auto p=row.value("where").toArray();
                rows<<QString("Nhấc · công đoạn %1 · A %2 · XYZ %3 / %4 / %5 · %6")
                    .arg(row.value("pass").toInt()+1).arg(row.value("a").toDouble(),0,'f',2).arg(p[0].toDouble()).arg(p[1].toDouble()).arg(p[2].toDouble())
                    .arg(row.value("reason").toString());
            }
            report->setPlainText(rows.isEmpty()?"Chưa có lỗi hoặc điểm tách vùng được ghi nhận.":rows.join('\n'));
            layout->addWidget(report);dlg.exec();
        });
        auto* modelMenu=menuBar()->addMenu("Mô hình");
        for(int i=0;i<3;++i){
            const auto axisName=QString("XYZ").mid(i,1);
            connect(modelMenu->addAction("Xoay "+axisName+" +90°"),&QAction::triggered,this,[this,i]{
                QMatrix4x4 m;QVector3D axis;axis[i]=1;m.rotate(90,axis);transform(m);
            });
        }
        connect(modelMenu->addAction("Khôi phục mẫu STL gốc 1:1"),&QAction::triggered,this,[this]{
            if(modelFile.isEmpty())return;
            history.append(view->modelTransform());
            view->setModelTransform(QMatrix4x4());
            modelChanged();view->fit();
        });
        auto* settings=menuBar()->addMenu("Thiết lập");
        connect(settings->addAction("Tự ôm phôi theo mẫu STL"),&QAction::triggered,this,[this]{
            if(fitStock(stock)){stockAuto=true;syncStock();changed();}
        });
        connect(settings->addAction("Mặt dẫn"),&QAction::triggered,this,[this]{dialog("surface");});
        connect(settings->addAction("Dựng mặt bọc STL"),&QAction::triggered,this,[this]{launchJob("wrap-stl",modelFile);});
        connect(settings->addAction("Nhập mặt Rhino .3dm…"),&QAction::triggered,this,[this]{
            auto file=QFileDialog::getOpenFileName(this,"Nhập mặt Rhino",{},"Rhino (*.3dm)");
            if(!file.isEmpty())launchJob("import-3dm",file);
        });
        auto* application=menuBar()->addMenu("Ứng dụng");
        auto* updates=application->addAction("Cập nhật 1-Click");
        bar->addAction(updates);
        connect(application->addAction("Nguồn thông báo Google Sheet…"),&QAction::triggered,this,[this]{
            const QString dir=QDir::homePath()+"/AppData/Local/XuanDieuCNC/ArtisanAxis";
#ifdef Q_OS_WIN
            const auto local=qEnvironmentVariable("LOCALAPPDATA");
            const auto base=local.isEmpty()?dir:local+"/XuanDieuCNC/ArtisanAxis";
#else
            Q_UNUSED(dir);
            const auto base=QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
#endif
            QFile settings(base+"/updates.json");
            QJsonObject values;
            if(settings.open(QIODevice::ReadOnly))values=QJsonDocument::fromJson(settings.readAll()).object();
            settings.close();
            QFile releaseConfig(QCoreApplication::applicationDirPath()+"/scripts/update-feed.json");
            QJsonObject release;
            if(releaseConfig.open(QIODevice::ReadOnly))release=QJsonDocument::fromJson(releaseConfig.readAll()).object();
            const bool pinned=!release.value("publisherSignerSha1").toString().trimmed().isEmpty();
            const QString policy=pinned
                ? QStringLiteral("Xác minh SHA256, phiên bản và chữ ký của nhà phát hành đã ghim.")
                : QStringLiteral("Xác minh SHA256 và phiên bản. Chỉ nhập nguồn CSV do xưởng quản lý.");
            bool accepted=false;
            auto url=QInputDialog::getText(this,"Cài đặt cập nhật",
                QStringLiteral("URL CSV đã xuất bản (để trống dùng GitHub)\n")+policy,
                QLineEdit::Normal,values.value("googleSheetCsv").toString(),&accepted).trimmed();
            if(!accepted)return;
            const QUrl address(url);
            if(!url.isEmpty()&&(!address.isValid()||address.scheme()!="https"||address.host()!="docs.google.com"||
                                !address.userInfo().isEmpty())){
                QMessageBox::warning(this,"Cập nhật","Chỉ nhận URL HTTPS từ docs.google.com.");return;
            }
            values["googleSheetCsv"]=url;
            QDir().mkpath(base);
            QSaveFile output(base+"/updates.json");
            if(!output.open(QIODevice::WriteOnly)||output.write(QJsonDocument(values).toJson())<0||!output.commit())
                QMessageBox::warning(this,"Cập nhật",output.errorString());
        });
        connect(updates,&QAction::triggered,this,[this,updates]{
#ifdef Q_OS_WIN
            if(worker.state()!=QProcess::NotRunning||loading){statusBar()->showMessage("Hãy đợi tác vụ hoàn tất trước khi cập nhật.");return;}
            const auto script=QCoreApplication::applicationDirPath()+"/scripts/Update.ps1";
            auto* update=new QProcess(this);
            updating=true;updates->setEnabled(false);
            auto finish=[this,updates,update](bool started){
                const auto result=QJsonDocument::fromJson(update->readAllStandardOutput()).object();
                update->deleteLater();
                updating=false;updates->setEnabled(true);
                if(result.value("ready").toBool()){close();return;}
                if(result.value("ok").toBool())statusBar()->showMessage("Ứng dụng đã ở phiên bản mới nhất.");
                else QMessageBox::warning(this,"Cập nhật",started?result.value("error").toString("Trình cập nhật không trả kết quả hợp lệ."):"Chưa mở được trình cập nhật.");
            };
            connect(update,&QProcess::finished,this,[finish](int,QProcess::ExitStatus){finish(true);});
            connect(update,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError e){if(e==QProcess::FailedToStart)finish(false);});
            update->start("powershell.exe",{"-NoProfile","-NonInteractive","-File",script,"-Apply","-ParentPid",QString::number(QCoreApplication::applicationPid())});
#else
            QMessageBox::information(this,"Cập nhật","Bộ cập nhật dành cho gói cài Windows.");
#endif
        });
        for(auto* control:bar->actions())jobControls.insert(control);
        for(auto* control:cameras->actions())jobControls.insert(control);
        for(auto* control:menuBar()->findChildren<QAction*>())jobControls.insert(control);
        jobControls.remove(cancelJob);
        connect(tree,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem* item,int){dialog(item->data(0,Qt::UserRole).toString(),item->data(0,Qt::UserRole+1).toInt());});
        connect(&worker,&QProcess::readyReadStandardError,this,[this]{drainProgress();});
        connect(&worker,&QProcess::started,this,[this]{
            if(jobCancelled){worker.kill();return;}
            worker.write(pendingJob);worker.closeWriteChannel();statusBar()->showMessage("CAMWorker đang xử lý…");
        });
        connect(&worker,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
            if(error!=QProcess::FailedToStart)return;
            progressTimer.stop();progressText->setText("Chưa khởi chạy được CAMWorker");
            setJobBusy(false);
            statusBar()->showMessage(jobCancelled?"Đã hủy tác vụ.":worker.errorString());
        });
        connect(&worker,&QProcess::finished,this,[this](int,QProcess::ExitStatus){
            drainProgress();progressTimer.stop();
            const auto unlock=qScopeGuard([this]{
                if(!jobNc.isEmpty()&&jobNc!=verifiedFile)QFile::remove(jobNc);
                setJobBusy(false);
            });
            const auto result=QJsonDocument::fromJson(worker.readAllStandardOutput()).object();
            if(jobCancelled){progressText->setText("Đã hủy tác vụ.");statusBar()->showMessage("Đã hủy tác vụ.");return;}
            if(jobRevision!=revision){statusBar()->showMessage("Cấu hình đã đổi trong lúc tính.");return;}
            if(!result.value("ok").toBool()){
                auto error=result.value("error").toString("Worker chưa trả về kết quả hợp lệ.");
                progressText->setText("Lỗi · "+error);statusBar()->showMessage(error);QMessageBox::warning(this,"CAMWorker",error);return;
            }
            progressBar->setValue(100);
            progressText->setText(QString("Đã hoàn tất · %1 giây").arg(jobClock.elapsed()/1000.0,0,'f',1));
            auto kind=result.value("kind").toString();
            if(kind=="motion"){
                const bool savedFile=result.value("fileVerification").toBool();
                if(!savedFile){
                    if(!verifiedFile.isEmpty())QFile::remove(std::exchange(verifiedFile,{}));
                    if(result.value("ncOnDisk").toBool())verifiedFile=jobNc;
                    nc=result.value("nc").toString().toUtf8();
                    lifts=result.value("lifts").toArray();
                    calculatedPaths.unite(jobPaths);lastNcPaths=jobPaths;
                    refresh();
                    if(!exportTarget.isEmpty()&&!saveNc(exportTarget))
                        QMessageBox::warning(this,"Xuất NC","Tệp chưa qua kiểm chứng hoặc không ghi được.");
                }
                ncPreview=result.value("ncPreview").toString();
                playback=result.value("playback").toArray();playhead=0;
                updatePlayback();
                cutOverlay=unpack(result.value("segments"));stages=result.value("stages").toArray();
                diagnostics=result.value("diagnostics").toArray();
                voxelPitch=result.value("pitch").toDouble();
                stagesBox->clear();stagesBox->addItem("Đường dao");
                for(auto v:stages){int i=v.toObject().value("group").toInt();stagesBox->addItem(i==0?"Sau phá mặt":i==1?"Sau cận tinh":"Sau chạy tinh");}
                statusBar()->showMessage(QString("%1 · %2 khối · %3 lớp không tới · %4 lần tách%5")
                    .arg(result.value("verified").toBool()?(savedFile?"Tệp NC đọc lại · ĐẠT theo mô hình":"ĐẠT theo mô hình"):"LỖI · mở Nhật ký NC")
                    .arg(result.value("blocks").toInt()).arg(result.value("unreachable").toInt()).arg(result.value("liftCount").toInteger())
                    .arg(result.value("calibratedOffset").toBool()?QString():QString(" · chưa khai báo offset máy")));
            }else if(kind=="plan"){
                nc=result.value("nc").toString().toUtf8();cutOverlay=unpack(result.value("segments"));
                statusBar()->showMessage(QString("4X · %1 điểm · %2 vị trí bị loại · NC để đối chiếu")
                    .arg(result.value("cuts").toInteger()).arg(result.value("rejected").toInteger()));
            }else{
                const auto nextGuide=kind=="rhino"?jobInput:QString{};
                if(guideFile!=nextGuide){guideFile=nextGuide;invalidateOutput();}
                surfaceOverlay=unpack(result.value("surface"));
                statusBar()->showMessage(kind=="rhino"?QString("Mặt Rhino đã nhập · %1 đối tượng hoặc mặt bỏ qua").arg(result.value("skipped").toInt()):
                                                   QString("Đã dựng mặt bọc tự động từ STL."));
            }
            view->setOverlay(cutOverlay,surfaceOverlay);
        });
        statusBar()->addPermanentWidget(summary);syncMachine();flow.preset(4);refresh();view->fit();
        restoreWorkspace();saveWorkspaceEnabled=true;
        if(!machine.loadWarning().isEmpty())statusBar()->showMessage(machine.loadWarning());
    }
    ~Workshop(){if(!loading)saveWorkspace();if(worker.state()!=QProcess::NotRunning){jobCancelled=true;worker.kill();worker.waitForFinished();}keepAwake(false);}
};
int main(int argc,char** argv){
    QSurfaceFormat format;format.setDepthBufferSize(24);format.setVersion(2,1);QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc,argv);
    applyWorkshopTheme(app);
    app.setOrganizationName("XuanDieuCNC");app.setApplicationName("ArtisanAxis");app.setApplicationVersion(ARTISAN_VERSION);
    Workshop window;window.show();
    const auto args=app.arguments();
    const int health=args.indexOf("--update-health-file"), token=args.indexOf("--update-health-token");
    if(health>0&&token>0&&health+1<args.size()&&token+1<args.size()&&QDir::isAbsolutePath(args[health+1])&&
       QRegularExpression("^[a-fA-F0-9]{32,128}$").match(args[token+1]).hasMatch()){
        const QString path=args[health+1],value=args[token+1];
        QTimer::singleShot(1500,&window,[&window,path,value]{
            if(!window.isVisible())return;
            QSaveFile ready(path);
            const auto bytes=value.toUtf8();
            if(ready.open(QIODevice::WriteOnly)&&ready.write(bytes)==bytes.size())ready.commit();
        });
    }
    return app.exec();
}
