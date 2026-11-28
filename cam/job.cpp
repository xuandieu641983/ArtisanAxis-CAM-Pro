#include "job.h"
#include "rhino.h"
#include "geometry.h"
#include "nc.h"
#include "motion.h"
#include "stock_sim.h"
#include <QFile>
#include <QSaveFile>
#include <numbers>
#include "../stl_reader.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
using namespace cam;
V point(const QJsonValue& v){
    const auto a=v.toArray();
    if(a.size()!=3||!a[0].isDouble()||!a[1].isDouble()||!a[2].isDouble())
        throw std::invalid_argument("Tọa độ XYZ thiếu thành phần.");
    return {a[0].toDouble(),a[1].toDouble(),a[2].toDouble()};
}
QJsonArray json(V p){return {p.x,p.y,p.z};}
double stockRadius(const StockSpec& stock,V pivot){
    const auto center=face((stock.bounds.lo+stock.bounds.hi)*.5,0,stock.axisY,pivot);
    const auto size=stock.bounds.hi-stock.bounds.lo;
    double a=(stock.axisY?size.x:size.y)*.5,b=size.z*.5;
    double y=std::abs(center.y),z=std::abs(center.z);
    if(!(size.x>0&&size.y>0&&size.z>0)||!std::isfinite(dot(size,size))||
       !std::isfinite(stock.endRatio)||stock.endRatio<0||stock.endRatio>1)
        throw std::invalid_argument("Kích thước phôi không hợp lệ.");
    if(stock.shape==0)return std::hypot(y+a,z+b);
    // The large end of the aligned frustum encloses its other cross-sections.
    if(a<b){std::swap(a,b);std::swap(y,z);}
    if(a==b)return std::hypot(y,z)+a;
    double lo=0,hi=std::numbers::pi/2;
    for(int i=0;i<80;++i){
        const double t=(lo+hi)*.5;
        if(b*z-a*y*std::tan(t)-(a*a-b*b)*std::sin(t)>0)lo=t;
        else hi=t;
    }
    const double t=(lo+hi)*.5;
    return std::hypot(y+a*std::cos(t),z+b*std::sin(t));
}
double planeForStock(const StockSpec& stock,V pivot,const QJsonObject& workflow,double requested){
    const double clearance=workflow.value("safeClearance").toDouble(50);
    if(!std::isfinite(clearance)||clearance<50||!std::isfinite(requested)||requested<50)
        throw std::invalid_argument("Chiều cao nhấc chưa được xác định.");
    return std::max(requested,stockRadius(stock,pivot)+clearance);
}
double phaseFeed(int angle,bool finish){
    if(angle>=0)return 450;
    return finish?600:550;
}
unsigned resolution(int v,int lo,int hi){return unsigned(std::clamp(v,lo,hi));}
std::vector<Tri> loadMesh(const QJsonObject& request){
    std::vector<float> raw;QString error;
    if(!readNativeSTL(request.value("file").toString(),raw,error))
        throw std::runtime_error(error.toStdString());
    const auto values=request.value("transformColumnMajor").toArray();
    if(values.size()!=16)throw std::invalid_argument("Thiếu ma trận mô hình.");
    double matrix[16];
    for(int i=0;i<16;++i)matrix[i]=values[i].toDouble();
    std::vector<Tri> mesh(raw.size()/9);
    for(size_t i=0;i<raw.size();i+=3){
        double x=raw[i],y=raw[i+1],z=raw[i+2];
        auto& q=mesh[i/9].p[(i/3)%3];
        q={matrix[0]*x+matrix[4]*y+matrix[8]*z+matrix[12],
           matrix[1]*x+matrix[5]*y+matrix[9]*z+matrix[13],
           matrix[2]*x+matrix[6]*y+matrix[10]*z+matrix[14]};
        if(!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z))
            throw std::invalid_argument("Biến đổi tạo tọa độ không hữu hạn.");
    }
    return mesh;
}
Wrap fitGuide(const Tree& tree,const QJsonObject& workflow,bool axisY,V pivot){
    return Wrap::fit(tree,resolution(workflow.value("wrapColumns").toInt(24),8,96),
                     resolution(workflow.value("wrapStacks").toInt(24),4,128),1,axisY,pivot);
}
QJsonArray guideTriangles(const Wrap& surface){
    constexpr unsigned n=64,m=48;
    V points[n+1][m+1];
    for(unsigned i=0;i<=n;++i)for(unsigned j=0;j<=m;++j)points[i][j]=surface.eval(double(i)/n,double(j)/m);
    QJsonArray faces;
    for(unsigned i=0;i<n;++i)for(unsigned j=0;j<m;++j){
        auto a=json(points[i][j]),b=json(points[i+1][j]),c=json(points[i+1][j+1]),d=json(points[i][j+1]);
        faces.append(a);faces.append(b);faces.append(c);faces.append(a);faces.append(c);faces.append(d);
    }
    return faces;
}
QJsonArray wrapMesh(std::vector<Tri> mesh,const QJsonObject& workflow,const QJsonObject& machine){
    const Tree tree(std::move(mesh));
    const auto rotary=machine.value("rotary").toArray();
    if(rotary.isEmpty())return guideTriangles(fitGuide(tree,workflow,false,{}));
    const auto a=rotary[0].toObject();
    const auto axis=point(a.value("direction"));
    if(a.value("role").toString()!="table"||axis.z!=0||!((axis.x==1&&axis.y==0)||(axis.x==0&&axis.y==1)))
        throw std::invalid_argument("Mặt dẫn tự động cần bàn A dọc X hoặc Y.");
    return guideTriangles(fitGuide(tree,workflow,axis.y==1,point(a.value("origin"))));
}
QJsonObject runMotionJob(const QJsonObject& request,std::vector<Tri> mesh,const QJsonObject& work,const JobProgress& progress){
    auto notify=[&](int value,const QString& name,qint64 turn=0,qint64 total=0){
        if(progress)progress(value,name,turn,total);
    };
    Machine m;
    const auto machine=request.value("machine").toObject();
    const auto rotary=machine.value("rotary").toArray();
    if(rotary.isEmpty())throw std::invalid_argument("Hồ sơ chưa khai báo bàn A.");
    const auto a=rotary[0].toObject();
    const auto dir=point(a.value("direction"));
    if(a.value("role").toString()!="table"||dir.z!=0||!((dir.x==1&&dir.y==0)||(dir.x==0&&dir.y==1)))
        throw std::invalid_argument("Lõi này cần A bàn dọc X hoặc Y.");
    m.pivot=point(a.value("origin"));m.axisY=dir.y==1;m.aSign=a.value("sign").toInt(1);
    m.axes=work.value("axes").toInt();
    if(request.value("operation").toString()=="verify-nc"){
        QFile input(request.value("ncFile").toString());
        if(input.open(QIODevice::ReadOnly)){
            for(int i=0;i<80&&!input.atEnd();++i){
                const auto line=input.readLine(2048);
                if(line.startsWith("G00 ")&&line.contains(" A")){m.axes=line.contains(" B")?5:4;break;}
            }
        }
    }
    m.safe=request.value("retract").toDouble();
    if((m.axes!=4&&m.axes!=5)||m.safe<50||!std::isfinite(m.safe))
        throw std::invalid_argument("Cấu hình 4/5 trục hoặc độ cao nhấc không hợp lệ.");
    auto offset=machine.value("workOffsetMachine");
    if(offset.isArray())m.machineOffset=point(offset);
    const auto lim=machine.value("limits").toObject();
    for(int i=0;i<m.axes;++i){
        auto bounds=lim.value(QString("XYZAB").mid(i,1)).toArray();
        if(bounds.size()==2)m.limits[i]=std::array{bounds[0].toDouble(),bounds[1].toDouble()};
    }
    if(m.axes==5){
        if(!m.limits[4])m.limits[4]=std::array{-20.,20.};
        if(rotary.size()!=2)throw std::invalid_argument("Thiếu đầu B.");
        const auto b=rotary[1].toObject(); const auto direction=point(b.value("direction"));
        const auto expected=m.axisY?V{-1,0,0}:V{0,1,0};
        if(b.value("role").toString()!="head"||dot(direction-expected,direction-expected)>1e-12)
            throw std::invalid_argument("Mốc 3 cần cấu trúc bàn A và đầu B vuông góc, theo quy ước trên hồ sơ.");
        m.bSign=b.value("sign").toInt(1);m.arm=machine.value("swingarmMm").toDouble();
        const auto measuredLever=machine.value("headPivotFromTip");
        if(!measuredLever.isUndefined()&&!measuredLever.isNull()){
            const auto lever=point(measuredLever);
            if(!(lever.z>0&&std::hypot(lever.x,lever.z)<=100000&&std::abs(lever.y)<=100000))
                throw std::invalid_argument("Vectơ từ mũi dao đến tâm B tại B=0 không hợp lệ.");
            m.headPivotFromTip=lever;
        }
        const auto mode=machine.value("rtcpMode").toString();
        if(mode=="G43.4"){
            const auto reg=machine.value("rtcpRegister").toString();
            if(!reg.isEmpty()&&reg!="H1")
                throw std::invalid_argument("Hồ sơ G43.4 cần H1 để tương thích post hiện tại.");
            m.dialect=Machine::Tcp434;
        }
        else if(mode=="TRAORI")m.dialect=Machine::Traori;
        else if(mode=="cam")m.dialect=Machine::Direct;
        else throw std::invalid_argument("Bộ đọc Mốc 3 chưa hỗ trợ hệ bù này.");
    }else m.dialect=Machine::Plain;
    const auto s=request.value("stock").toObject();const double e=s.value("expansion").toDouble();
    auto shape=s.value("shape").toString();
    StockSpec stock{{point(s.value("min"))-V{e,e,e},point(s.value("max"))+V{e,e,e}},
                    shape=="cylinder"?1:shape=="frustum"?2:0,s.value("endRatio").toDouble(.5),m.axisY};
    m.safe=planeForStock(stock,m.pivot,work,m.safe);
    const auto limitZ=m.limits[2];
    const double datumZ=m.pivot.z+m.machineOffset.z;
    if(limitZ&&request.value("operation").toString()!="verify-nc"){
        const double liftZ=datumZ+m.safe+.20;
        if(liftZ<(*limitZ)[0]||liftZ>(*limitZ)[1])
            throw std::invalid_argument("Mức nhấc theo kích thước phôi vượt giới hạn mềm Z ở tư thế B=0.");
    }
    std::vector<Cutter> tools;
    for(auto value:work.value("tools").toArray()){
        auto t=value.toObject();
        tools.push_back({t.value("diameter").toDouble(),t.value("tipRadius").toDouble(),t.value("singleSideHalfAngle").toDouble(),
                         t.value("overhang").toDouble(),t.value("flute").toDouble(),t.value("holderDiameter").toDouble()*.5});
    }
    if(tools.empty())throw std::invalid_argument("Thư viện dao rỗng.");
    std::vector<PathSpec> paths,raster;std::vector<int> groups;QStringList names;
    std::optional<double> limitedRasterFloor;
    for(auto value:work.value("paths").toArray()){
        auto p=value.toObject();if(!p.value("active").toBool())continue;
        int angle=p.value("faceAngle").toInt(),tool=p.value("toolIndex").toInt(-1);
        if(tool<0||size_t(tool)>=tools.size())throw std::invalid_argument("Thiếu dao cho công đoạn.");
        PathSpec path{tool,angle,p.value("step").toDouble(),p.value("allowance").toDouble(),p.value("allowance").toDouble()==0.};
        path.pointSpacing=p.value("pointSpacing").toDouble();
        path.stepDown=p.value("stepDown").toDouble();
        path.minZ=p.value("zMin").toDouble(path.minZ);
        path.maxZ=p.value("zMax").toDouble(path.maxZ);
        path.clipMin=p.value("clipMin").toDouble(path.clipMin);
        path.clipMax=p.value("clipMax").toDouble(path.clipMax);
        path.bMin=p.value("bMin").toDouble(-20);
        path.bMax=p.value("bMax").toDouble(20);
        path.lead=p.value("lead").toDouble();
        path.lean=p.value("lean").toDouble();
        path.feed=p.value("feed").toDouble(phaseFeed(angle,path.finish));
        path.rpm=p.value("rpm").toInt(16000);
        path.reverse=p.value("reverse").toBool();
        path.axes=p.value("axes").toInt(m.axes);
        if(path.minZ>path.maxZ||path.clipMin>=path.clipMax||path.bMin>0||path.bMax<0||
           std::abs(path.lead)>60||std::abs(path.lean)>60||
           path.bMin< -60||path.bMax>60||path.feed<=0||path.rpm<=0||path.rpm>60000||(path.axes>m.axes&&request.value("operation").toString()!="verify-nc"))
            throw std::invalid_argument("Thông số đường dao không hợp lệ hoặc không tương thích post.");
        if(angle>=0){
            if(limitZ){
                // Indexed roughing always has B=0. Account for the 0.025 mm
                // clearance added to emitted raster points and NC rounding.
                const double floor=(*limitZ)[0]-datumZ;
                const double previousFloor=std::max(path.minZ,work.value("faces").toInt()==2?-1.:1.);
                if(floor>previousFloor){
                    path.minZ=floor;
                    limitedRasterFloor=floor;
                }
                path.maxZ=std::min(path.maxZ,(*limitZ)[1]-datumZ-.026);
            }
            raster.push_back(path);
        }
        paths.push_back(path);
        names.append(p.value("name").toString("Đường xoay"));
        groups.push_back(angle>=0?0:path.finish?2:1);
    }
    for(size_t i=0,write=0;i<paths.size();++i){
        if(paths[i].angle<0)continue;
        std::rotate(paths.begin()+write,paths.begin()+i,paths.begin()+i+1);
        std::rotate(groups.begin()+write,groups.begin()+i,groups.begin()+i+1);
        names.move(qsizetype(i),qsizetype(write));
        ++write;
    }
    if(paths.empty()&&request.value("operation").toString()!="verify-nc")
        throw std::invalid_argument("Hãy tích ít nhất một đường dao trước khi tính.");
    notify(5,"Dựng BVH và lưới khoảng cách");
    Tree tree(raster.empty()?std::move(mesh):mesh);auto bounds=tree.bounds();const auto span=bounds.hi-bounds.lo;
    if(std::min({span.x,span.y,span.z})<=0)throw std::invalid_argument("Hãy dùng STL hình khối có ba chiều.");
    auto sdf=DistanceGrid::measure(tree,bounds,std::max({std::cbrt(span.x*span.y*span.z/14000.),span.x/90,span.y/90,span.z/90,.8}));
    Spiral program;std::string bytes;
    QJsonArray driveSurface;
    if(request.value("operation").toString()=="verify-nc"){
        QFile f(request.value("ncFile").toString());
        if(!f.open(QIODevice::ReadOnly)||f.size()>qint64(1024)*1024*1024)throw std::invalid_argument("Không mở được NC hoặc kích thước quá giới hạn.");
        bytes=f.readAll().toStdString();
    }else{
        if(!raster.empty()){
            notify(12,"Phá mặt raster");
            auto pre=generate(mesh,stock,tools,raster,work.value("faces").toInt()==2,m.pivot,m.safe,
                [&](size_t pass,size_t row,size_t total){
                    notify(12+int(6*(double(pass)+double(row)/total)/raster.size()),
                           QString("%1 · %2 %3/%4").arg(names.at(qsizetype(pass)))
                               .arg(work.value("faces").toInt()==2?"hàng":"lớp Z").arg(row).arg(total));
                });
            Pose previous{};
            int oldTool=-1,oldPass=-1;
            const auto retractKind=m.axes==5?MotionKind::Retract:MotionKind::Rapid;
            const double retractZ=m.safe+(m.axes==5?.20:0.);
            for(auto c:pre.cuts){
                Pose target{c.point,c.angle,0};target.tip.z+=.025;
                Pose raise=target;raise.tip.z=m.safe;
                if(!c.link||c.tool!=oldTool||c.pass!=oldPass){
                    if(oldTool>=0){auto leave=previous;leave.tip.z=retractZ;program.moves.push_back({leave,oldTool,oldPass,retractKind});}
                    program.moves.push_back({raise,c.tool,c.pass,MotionKind::Rapid});
                }
                program.moves.push_back({target,c.tool,c.pass,c.link?MotionKind::Cut:MotionKind::Approach});
                previous=target;oldTool=c.tool;oldPass=c.pass;
            }
            if(oldTool>=0){previous.tip.z=retractZ;program.moves.push_back({previous,oldTool,oldPass,retractKind});}
            for(auto& move:program.moves){move.feed=paths[move.pass].feed;move.rpm=paths[move.pass].rpm;}
            program.unreachable+=pre.rejected;
        }
        auto rhinoFile=request.value("guideFile").toString();
        std::vector<V> external;
        std::optional<Wrap> wrap;
        if(paths.size()>raster.size()){
            if(rhinoFile.isEmpty()){
                wrap=fitGuide(tree,work,m.axisY,m.pivot);
                driveSurface=guideTriangles(*wrap);
            }else{
                auto result=readRhinoSurface(rhinoFile);
                if(!result.value("ok").toBool())throw std::runtime_error(result.value("error").toString().toStdString());
                const auto normals=result.value("guide").toArray();
                external.reserve(size_t(normals.size()));
                for(const auto sample:normals)external.push_back(point(sample));
                driveSurface=result.value("surface").toArray();
            }
        }
        GuideNormal normal;
        if(external.size()==49*49)normal=[samples=std::move(external)](double u,double v){
            const double fu=(u-std::floor(u))*48,fv=std::clamp(v,0.,1.)*48;
            const int i=std::min(47,int(fu)),j=std::min(47,int(fv));
            auto n=[&](int x,int y){return samples[size_t(49*y+x)];};
            const auto low=n(i,j)*(1-(fu-i))+n(i+1,j)*(fu-i);
            const auto high=n(i,j+1)*(1-(fu-i))+n(i+1,j+1)*(fu-i);
            return unit(low*(1-(fv-j))+high*(fv-j));
        };
        DriveGuide drive;
        if(wrap)drive=[&](double u,double axial){
            DriveSample sample;
            sample.point=wrap->eval(u,wrap->axialParameter(axial),&sample.normal);
            return sample;
        };
        for(size_t pass=raster.size();pass<paths.size();++pass){
            int last=-1;size_t lastTurn=0;
            auto plan=planSpiral(tree,m,tools.at(paths[pass].tool),paths[pass],int(pass),normal,&sdf,
                [&](size_t done,size_t total,size_t perTurn){
                    const int percent=18+int(58*(double(pass-raster.size())+double(done)/total)/std::max(size_t(1),paths.size()-raster.size()));
                    const size_t turn=(done==total?(total+perTurn-1):done)/perTurn;
                    if(percent!=last||turn>=lastTurn+8||done==total){
                        notify(percent,names.at(qsizetype(pass)),qint64(turn),qint64((total+perTurn-1)/perTurn));
                        last=percent;lastTurn=turn;
                    }
                },drive);
            program.unreachable+=plan.unreachable;
            if(!program.moves.empty()&&!plan.moves.empty()){
                double offsetA=360*std::round((program.moves.back().pose.a-plan.moves.front().pose.a)/360.);
                if(!m.limits[3]){
                    for(auto& move:plan.moves)move.pose.a+=offsetA;
                    for(auto& lift:plan.lifts)lift.location.a+=offsetA;
                }
            }
            if(program.moves.empty())program.moves=std::move(plan.moves);
            else program.moves.insert(program.moves.end(),plan.moves.begin(),plan.moves.end());
            program.lifts.insert(program.lifts.end(),plan.lifts.begin(),plan.lifts.end());
        }
        notify(78,"Ghi G-Code");
        bytes=writeMotionNc(program,m,work.value("post").toString().toStdString());
    }
    const auto liftRecords=std::move(program.lifts);
    std::vector<Motion>().swap(program.moves);
    notify(82,"Kiểm chứng NC");
    MotionChecks checks(tree,m,&sdf);
    int verificationPercent=82;
    auto verified=verifyMotionNc(bytes,m,tools,checks,[&](size_t read,size_t total){
        const int current=82+int(10*(double(read)/total));
        if(current!=verificationPercent){
            notify(current,"Kiểm chứng NC");
            verificationPercent=current;
        }
    });
    QJsonArray diagnostics;
    auto add=[&](int line,const QString& msg){if(diagnostics.size()<500)diagnostics.append(QJsonObject{{"line",line},{"message",msg}});};
    if(limitedRasterFloor)
        add(0,QString("Phá mặt dừng ở Z cục bộ %1 mm theo giới hạn hành trình mềm. Vật liệu thấp hơn được giữ lại.").arg(*limitedRasterFloor,0,'f',3));
    for(const auto& error:verified.errors){
        if(error.failure==SweepFailure::Travel)
            add(int(error.line),QStringLiteral("Chuyển động vượt giới hạn hành trình hoặc xuống dưới mặt phẳng an toàn."));
        else if(error.failure==SweepFailure::Assembly)
            add(int(error.line),QStringLiteral("Phát hiện giao cắt bao dao với mô hình STL tại một tư thế của đoạn."));
        else if(error.failure==SweepFailure::Unresolved)
            add(int(error.line),QStringLiteral("Chưa chứng minh được khoảng hở liên tục của đoạn chuyển động."));
        else add(int(error.line),QString::fromStdString(error.message));
    }
    Spiral reconstructed;reconstructed.moves=std::move(verified.motions);
    StockSim sim;
    notify(93,"Mô phỏng phôi");
    if(verified.errors.empty())sim=simulateStock(stock,reconstructed,m,tools,groups,tree);
    QJsonArray stages;
    double gougeDepth=0;
    for(auto& stage:sim.stages){
        QJsonArray shell, excessMap,gougeMap;
        for(auto p:stage.shell)shell.append(json(p));
        for(auto p:stage.excessMap)excessMap.append(json(p));
        for(auto p:stage.gougeMap){
            gougeMap.append(json(p));
            gougeDepth=std::max(gougeDepth,tree.distance(p));
        }
        stages.append(QJsonObject{{"group",stage.group},{"remaining",qint64(stage.remaining)},
            {"excess",qint64(stage.excess)},{"gouge",qint64(stage.gouge)},{"shell",shell},{"excessMap",excessMap},{"gougeMap",gougeMap}});
    }
    for(auto line:sim.warningMoves)add(int(line),QStringLiteral("Mô phỏng voxel phát hiện thân/collet hoặc chuyển động nhanh cắt phôi."));
    QJsonArray lifts;
    for(auto& l:liftRecords){if(lifts.size()>=1000)break;lifts.append(QJsonObject{{"pass",l.pass},{"where",json(partPoint(l.location,m))},{"a",l.location.a},{"reason",QString::fromStdString(l.reason)}});}
    QJsonArray display;const auto& moves=reconstructed.moves;
    const size_t stride=std::max(size_t(1),(moves.size()+50000)/50000);
    for(size_t i=1;i<moves.size();i+=stride)if(moves[i].kind==MotionKind::Cut||moves[i].kind==MotionKind::Approach){
        display.append(json(partPoint(moves[i-1].pose,m)));display.append(json(partPoint(moves[i].pose,m)));
    }
    QJsonArray playback;
    constexpr size_t maxFrames=12000;
    const size_t frameStep=std::max(size_t(1),(moves.size()+maxFrames-1)/maxFrames);
    double seconds=0;
    for(size_t i=0;i<moves.size();++i){
        const auto& move=moves[i];
        if(i){
            const auto delta=partPoint(move.pose,m)-partPoint(moves[i-1].pose,m);
            seconds+=60*std::sqrt(dot(delta,delta))/move.feed;
        }
        if(i%frameStep==0||i+1==moves.size()){
            auto tip=world(move.pose.tip,0,m.axisY,m.pivot);
            playback.append(QJsonArray{seconds,tip.x,tip.y,tip.z,move.pose.a,move.pose.b,move.tool});
        }
    }
    // The 0.004 mm NC tolerance also bounds ambiguity of centers sitting
    // almost exactly on the STL. Deeper removed model centers block export.
    if(gougeDepth>.004)
        add(0,QString("Mô phỏng loại voxel nằm sâu %1 mm trong mẫu STL.").arg(gougeDepth,0,'f',4));
    bool ok=verified.errors.empty()&&sim.stockWarnings==0&&gougeDepth<=.004;
    const bool calibrated=offset.isArray()&&m.limits[2].has_value()&&machine.value("offsetConfirmed").toBool(false);
    if(!calibrated)add(0,QStringLiteral("Chưa xác nhận offset hệ máy hoặc thiếu giới hạn Z. Chỉ xem trước theo mô hình, chưa khóa được hành trình lúc chạy thật."));
    const bool exportable=ok&&calibrated&&request.value("operation").toString()!="verify-nc";
    const QString outputPath=request.value("ncOutput").toString();
    if(exportable&&!outputPath.isEmpty()){
        QSaveFile out(outputPath);
        if(!out.open(QIODevice::WriteOnly)||out.write(bytes.data(),qint64(bytes.size()))!=qint64(bytes.size())||!out.commit())
            throw std::runtime_error("Không ghi được tệp NC.");
    }
    notify(100,"Đã hoàn tất");
    return {{"ok",true},{"kind","motion"},{"fileVerification",request.value("operation").toString()=="verify-nc"},{"verified",ok},{"cuts",int(moves.size())},{"blocks",int(verified.blocks)},{"surface",driveSurface},
            {"unreachable",int(program.unreachable)},{"segments",display},{"lifts",lifts},{"diagnostics",diagnostics},
            {"stages",stages},{"pitch",sim.pitch},{"calibratedOffset",calibrated},
            {"playback",playback},{"seconds",seconds},{"liftCount",qint64(liftRecords.size())},{"safePlane",m.safe},
            {"ncOnDisk",exportable&&!outputPath.isEmpty()},
            {"ncPreview",QString::fromLatin1(bytes.data(),qsizetype(std::min<size_t>(bytes.size(),16000)))},
            {"nc",exportable&&outputPath.isEmpty()?QString::fromStdString(bytes):QString()}};
}

}
QJsonObject runCamJob(const QJsonObject& request,const JobProgress& progress){
    const auto op=request.value("operation").toString();
    if(op=="import-3dm")return readRhinoSurface(request.value("file").toString());
    if(progress)progress(1,"Đọc STL",0,0);
    auto mesh=loadMesh(request);
    const auto work=request.value("workflow").toObject();
    if(op=="plan-motion"||op=="verify-nc")return runMotionJob(request,std::move(mesh),work,progress);
    if(op=="wrap-stl"){
        if(progress)progress(15,"Dựng mặt bọc NURBS",0,0);
        auto surface=wrapMesh(std::move(mesh),work,request.value("machine").toObject());
        if(progress)progress(100,"Đã dựng mặt bọc",0,0);
        return {{"ok",true},{"surface",surface},{"kind","wrap"}};
    }
    if(op!="plan-4x"||work.value("axes").toInt()!=4)
        throw std::invalid_argument("Mốc 2 cần chọn chế độ chạy 4 trục trong Trợ lý.");
    const auto machine=request.value("machine").toObject();
    const auto rotary=machine.value("rotary").toArray();
    if(rotary.isEmpty())throw std::invalid_argument("Máy chưa có trục quay.");
    const auto a=rotary.at(0).toObject();
    auto direction=point(a.value("direction"));
    if(a.value("role").toString()!="table"||direction.z!=0||!((direction.x==1.&&direction.y==0.)||(direction.x==0.&&direction.y==1.)))
        throw std::invalid_argument("Mốc 2 hỗ trợ bàn A dọc +X hoặc +Y, đổi chiều bằng dấu trục.");
    bool alongY=direction.y==1.;
    auto pivot=point(a.value("origin"));
    const auto s=request.value("stock").toObject();
    double extension=s.value("expansion").toDouble();
    auto low=point(s.value("min"))-V{extension,extension,extension}, high=point(s.value("max"))+V{extension,extension,extension};
    if(!(high.x>low.x&&high.y>low.y&&high.z>low.z))throw std::invalid_argument("Kích thước phôi không hợp lệ.");
    auto shape=s.value("shape").toString();
    StockSpec stock{{low,high},shape=="cylinder"?1:shape=="frustum"?2:0,s.value("endRatio").toDouble(.5),alongY};
    std::vector<Cutter> tools;
    for(auto value:work.value("tools").toArray()){
        auto t=value.toObject();
        tools.push_back({t.value("diameter").toDouble(),t.value("tipRadius").toDouble(),t.value("singleSideHalfAngle").toDouble(),
                          t.value("overhang").toDouble(),t.value("flute").toDouble(),t.value("holderDiameter").toDouble()*.5});
    }
    std::vector<PathSpec> paths;
    double lift=planeForStock(stock,pivot,work,request.value("retract").toDouble());
    for(auto value:work.value("paths").toArray()){
        auto p=value.toObject();if(!p.value("active").toBool())continue;
        int angle=p.value("faceAngle").toInt(), tool=p.value("toolIndex").toInt(-1);
        if(p.value("axes").toInt()!=4||tool<0||size_t(tool)>=tools.size())throw std::invalid_argument("Công đoạn không tương thích 4X.");
        paths.push_back({tool,angle,p.value("step").toDouble(),p.value("allowance").toDouble(),p.value("allowance").toDouble()==0.});
    }
    auto plan=generate(mesh,stock,tools,paths,work.value("faces").toInt()==2,pivot,lift);
    if(plan.cuts.empty())throw std::runtime_error("Không có điểm cắt đạt bộ lọc hình học. Kiểm tra dao, phôi và vòng chống tâm.");
    auto nc=writeNc(plan,work.value("post").toString().toStdString(),alongY,pivot,a.value("sign").toInt());
    QJsonArray display;
    const size_t stride=std::max(size_t(1),(plan.cuts.size()+50000)/50000);
    for(size_t i=0;i<plan.cuts.size();i+=stride){
        auto c=plan.cuts[i];
        auto start=c.point;
        if(c.link&&i)start=plan.cuts[i-1].point;else start.z=std::max(plan.safe,plan.safe-pivot.z);
        display.append(json(world(start,c.angle,alongY,pivot)));
        display.append(json(world(c.point,c.angle,alongY,pivot)));
    }
    return {{"ok",true},{"kind","plan"},{"cuts",qint64(plan.cuts.size())},{"rejected",qint64(plan.rejected)},
            {"segments",display},{"nc",QString::fromStdString(nc)}};
}
