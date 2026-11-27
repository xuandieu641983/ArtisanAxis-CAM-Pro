#include <QtWidgets>
#include <QOpenGLContext>
#include <QJsonArray>
#include <QProcess>
#include <QJsonDocument>
#include <cmath>
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

#ifndef ARTISAN_VERSION
#define ARTISAN_VERSION "1.0.0"
#endif

class Workshop final : public QMainWindow {
    friend struct WorkshopAudit;
    View* view;
    QTreeWidget* tree=new QTreeWidget;
    QLabel* summary=new QLabel;
    MachineProfiles machine{this};
    Workflow flow;
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
            {"transform",modelTransform()},{"stock",stockJson()},{"workflow",flow.snapshot()}}).toJson(QJsonDocument::Compact);
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
        ++revision;nc.clear();cutOverlay.clear();surfaceOverlay.clear();
        view->setOverlay({},{});view->setSimStock({},{},{});stages={};diagnostics={};lifts={};stagesBox->clear();refresh();saveWorkspace();
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
            .arg(extent(0),0,'f',4).arg(extent(1),0,'f',4).arg(extent(2),0,'f',4).arg(r,0,'f',3).arg(r+50,0,'f',3));
    }
    void syncStock(){
        auto extent=[&](int i){return stock.max[i]-stock.min[i]+2*stock.expansion;};
        double r=radius(stock);flow.setStock(extent(0),extent(1),extent(2),r,r+50);drawStock(stock);
    }
    void syncMachine(){flow.setMachine(machine.axisCount(),[this](int axes){return machine.postForAxes(axes);});syncStock();}
    void changed(){flow.invalidateAll();invalidateOutput();}
    void refresh(){
        tree->clear();
        auto branch=[&](QString label,QString key,QStringList children){
            auto* root=new QTreeWidgetItem(tree,{label});root->setData(0,Qt::UserRole,key);
            for(int i=0;i<children.size();++i){auto* child=new QTreeWidgetItem(root,{children[i]});child->setData(0,Qt::UserRole,key);child->setData(0,Qt::UserRole+1,i);}
            root->setExpanded(true);
        };
        branch("Mô hình","model",{modelFile.isEmpty()?QString("Chưa nạp STL"):QFileInfo(modelFile).fileName()});
        branch("Phôi","stock",{stock.shape=="box"?"Hộp":stock.shape=="cylinder"?"Trụ":"Chóp cụt"});
        branch("Thư viện dao","tools",flow.toolNames());branch("Mặt dẫn","surface",{});
        branch("Đường dao","paths",flow.pathNames());branch("Máy và post","machine",{machine.name()});
        branch("Trợ lý CAM","ai",{});
    }
    void load(){
        QString f=QFileDialog::getOpenFileName(this,"Mở mô hình STL",{},"STL (*.stl *.STL)");
        if(f.isEmpty())return;
        loading=true;const auto request=++loadRequest;
        statusBar()->showMessage("Đang đọc lưới STL…");
        view->loadSTLAsync(f,[this,f,request](bool ok,QString error){
            if(request!=loadRequest)return;
            loading=false;
            if(!ok){QMessageBox::warning(this,"Đọc STL",error);return;}
            modelFile=f;guideFile.clear();history.clear();view->setModelTransform(QMatrix4x4());changed();view->fit();
            statusBar()->showMessage(QString("Đã nạp %1 tam giác").arg(view->triangleCount()));
        });
    }
    void transform(const QMatrix4x4& m){if(modelFile.isEmpty())return;history.push_back(view->modelTransform());view->setModelTransform(m*history.last());changed();}
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
        if(key=="model"){model();return;}
        if(key=="stock"){
            view->setSimStock({},{},{});stagesBox->setCurrentIndex(0);
            const auto original=stock;
            if(editStock(this,stock,alongY(),machine.rotaryOrigin(),machine.rotaryDirection(),[this](const Stock& s){drawStock(s);})){
                syncStock();changed();
            }else drawStock(original);
        }else if(key=="machine"){if(machine.edit(this)){syncMachine();changed();}}
        else {
            bool saved=false;
            if(key=="tools")saved=flow.editTools(this);
            if(key=="paths")saved=flow.editPath(this,index);
            if(key=="surface")saved=flow.editSurface(this);
            if(key=="ai")saved=flow.editAI(this);
            if(saved)invalidateOutput();
        }
    }
    void launchJob(QString operation,QString file,QString extra={}){
        if(loading||file.isEmpty()){statusBar()->showMessage("Hãy nạp xong một STL trước khi tính.");return;}
        if(worker.state()!=QProcess::NotRunning||updating)return;
        pendingJob=QJsonDocument(QJsonObject{{"operation",operation},{"file",file},
            {"machine",machine.current()},{"guideFile",guideFile},{"ncFile",extra},{"stock",stockJson()},{"workflow",flow.snapshot()},{"retract",radius(stock)+50},{"transformColumnMajor",modelTransform()}}).toJson(QJsonDocument::Compact)+"\n";
        jobRevision=revision;QString program=QCoreApplication::applicationDirPath()+"/CAMWorker";
#ifdef Q_OS_WIN
        program+=".exe";
#endif
        worker.setProgram(program);worker.start();
    }
    void exportNc(){
        if(worker.state()!=QProcess::NotRunning||loading||updating)return;
        if(nc.isEmpty()){statusBar()->showMessage("Chưa có NC đã qua kiểm tra mô hình.");return;}
        auto reply=QMessageBox::warning(this,"Xuất NC đối chiếu",
            "Tệp đã qua bộ đọc theo mô hình kế hoạch. Cần đối chiếu tọa độ, post, giới hạn và đồ gá trên máy; dùng NC này để kiểm tra.",
            QMessageBox::Save|QMessageBox::Cancel,QMessageBox::Cancel);
        if(reply!=QMessageBox::Save)return;
        auto file=QFileDialog::getSaveFileName(this,"Lưu NC đối chiếu","ArtisanAxis_M3_REVIEW.nc","NC (*.nc *.tap)");
        if(file.isEmpty())return;
        QSaveFile out(file);
        if(!out.open(QIODevice::WriteOnly)||out.write(nc)!=nc.size()||!out.commit()){
            QMessageBox::warning(this,"Lưu NC",out.errorString());return;
        }
        launchJob("verify-nc",modelFile,file);
    }
public:
    Workshop(){
        setWindowTitle("ArtisanAxis CAM Pro · Xuân Diệu CNC · v" ARTISAN_VERSION);
        resize(1240,790);view=new View;setCentralWidget(view);
        auto* dock=new QDockWidget("Dự án",this);tree->setHeaderHidden(true);dock->setWidget(tree);addDockWidget(Qt::LeftDockWidgetArea,dock);
        auto* bar=addToolBar("Xưởng");bar->setMovable(false);
        auto action=[&](QString name,auto fn){auto* a=bar->addAction(name);connect(a,&QAction::triggered,this,fn);return a;};
        action("Mở STL",[this]{load();})->setShortcut(QKeySequence::Open);
        action("Biến đổi",[this]{model();});action("Lấy gốc X0",[this]{base();});
        action("Hoàn tác",[this]{if(!history.isEmpty()){view->setModelTransform(history.takeLast());changed();}})->setShortcut(QKeySequence::Undo);
        action("Phôi",[this]{dialog("stock");});action("Dao",[this]{dialog("tools");});action("Máy / Post",[this]{dialog("machine");});
        action("Trợ lý",[this]{dialog("ai");});action("2 mặt + tinh",[this]{flow.preset(2);invalidateOutput();});
        action("4 mặt + tinh",[this]{flow.preset(4);invalidateOutput();});action("Tính 4X/5X",[this]{launchJob("plan-motion",modelFile);});
        action("Xuất NC",[this]{exportNc();});
        action("Đọc lại NC",[this]{
            auto file=QFileDialog::getOpenFileName(this,"Đọc NC xuất từ Mốc 3",{},"NC (*.nc *.tap)");
            if(!file.isEmpty())launchJob("verify-nc",modelFile,file);
        });
        action("Hủy tác vụ",[this]{if(worker.state()!=QProcess::NotRunning){++revision;worker.kill();}});
        auto* cameras=addToolBar("Khung nhìn");
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
            changed();view->fit();
        });
        auto* settings=menuBar()->addMenu("Thiết lập");
        connect(settings->addAction("Mặt dẫn"),&QAction::triggered,this,[this]{dialog("surface");});
        connect(settings->addAction("Dựng mặt bọc STL"),&QAction::triggered,this,[this]{if(worker.state()==QProcess::NotRunning){guideFile.clear();invalidateOutput();launchJob("wrap-stl",modelFile);}});
        connect(settings->addAction("Nhập mặt Rhino .3dm…"),&QAction::triggered,this,[this]{
            auto file=QFileDialog::getOpenFileName(this,"Nhập mặt Rhino",{},"Rhino (*.3dm)");
            if(!file.isEmpty()){
                if(worker.state()!=QProcess::NotRunning)return;
                invalidateOutput();guideFile=file;launchJob("import-3dm",file);
            }
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
        connect(tree,&QTreeWidget::itemDoubleClicked,this,[this](QTreeWidgetItem* item,int){dialog(item->data(0,Qt::UserRole).toString(),item->data(0,Qt::UserRole+1).toInt());});
        connect(&worker,&QProcess::stateChanged,this,[this](QProcess::ProcessState state){keepAwake(state!=QProcess::NotRunning);});
        connect(&worker,&QProcess::started,this,[this]{worker.write(pendingJob);worker.closeWriteChannel();statusBar()->showMessage("CAMWorker đang xử lý…");});
        connect(&worker,&QProcess::errorOccurred,this,[this](QProcess::ProcessError){statusBar()->showMessage(worker.errorString());});
        connect(&worker,&QProcess::finished,this,[this](int,QProcess::ExitStatus){
            const auto result=QJsonDocument::fromJson(worker.readAllStandardOutput()).object();
            if(jobRevision!=revision){statusBar()->showMessage("Tác vụ bị hủy hoặc cấu hình đã đổi.");return;}
            if(!result.value("ok").toBool()){
                auto error=result.value("error").toString("Worker chưa trả về kết quả hợp lệ.");
                statusBar()->showMessage(error);QMessageBox::warning(this,"CAMWorker",error);return;
            }
            auto kind=result.value("kind").toString();
            if(kind=="motion"){
                const bool savedFile=result.value("fileVerification").toBool();
                if(!savedFile){
                    nc=result.value("nc").toString().toUtf8();
                    lifts=result.value("lifts").toArray();
                }else if(!result.value("verified").toBool())nc.clear();
                cutOverlay=unpack(result.value("segments"));stages=result.value("stages").toArray();
                diagnostics=result.value("diagnostics").toArray();
                voxelPitch=result.value("pitch").toDouble();
                stagesBox->clear();stagesBox->addItem("Đường dao");
                for(auto v:stages){int i=v.toObject().value("group").toInt();stagesBox->addItem(i==0?"Sau phá mặt":i==1?"Sau cận tinh":"Sau chạy tinh");}
                statusBar()->showMessage(QString("%1 · %2 khối · %3 lớp không tới · %4 lần tách%5")
                    .arg(result.value("verified").toBool()?(savedFile?"Tệp NC đọc lại · ĐẠT theo mô hình":"ĐẠT theo mô hình"):"LỖI · mở Nhật ký NC")
                    .arg(result.value("blocks").toInt()).arg(result.value("unreachable").toInt()).arg(lifts.size())
                    .arg(result.value("calibratedOffset").toBool()?QString():QString(" · chưa khai báo offset máy")));
            }else if(kind=="plan"){
                nc=result.value("nc").toString().toUtf8();cutOverlay=unpack(result.value("segments"));
                statusBar()->showMessage(QString("4X · %1 điểm · %2 vị trí bị loại · NC để đối chiếu")
                    .arg(result.value("cuts").toInteger()).arg(result.value("rejected").toInteger()));
            }else{
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
    ~Workshop(){if(!loading)saveWorkspace();if(worker.state()!=QProcess::NotRunning){worker.kill();worker.waitForFinished();}keepAwake(false);}
};
int main(int argc,char** argv){
    QSurfaceFormat format;format.setDepthBufferSize(24);format.setVersion(2,1);QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc,argv);
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
