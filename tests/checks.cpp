#include "profiles.h"
#include "stock.h"
#include "workflow.h"
#include <QApplication>
#include <QJsonArray>
#include <QFileInfo>
#include <cstdlib>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
static void check(bool valid,const char* message){
    if(!valid){std::cerr<<message<<'\n';std::exit(1);}
}
int main(int argc,char** argv){
    QApplication app(argc,argv);
    QTemporaryDir profileHome;
    check(profileHome.isValid(),"Temporary profile directory");
    qputenv("XDG_CONFIG_HOME",profileHome.path().toUtf8());
    // A unique Windows organization also keeps this isolated from the production profile.
    app.setOrganizationName("ArtisanAxisM1Tests");
    app.setApplicationName(QFileInfo(profileHome.path()).fileName());
    MachineProfiles profiles;
    check(profiles.name().contains("CM600"),"CM600 default profile");
    const auto offset=profiles.current().value("workOffsetMachine").toArray();
    check(offset==QJsonArray{264.607,455.104,-470.090},"CM600 reference offset");
    check(!profiles.current().value("offsetConfirmed").toBool(),"Reference offset is not measured calibration");
    check(profiles.axisCount()==5,"CM600 axis count");
    check(profiles.current().value("swingarmMm").toDouble()==232.918,"CM600 arm");
    auto malformed=profiles.current();
    malformed["headPivotFromTip"]=QJsonArray{78.7};
    check(!MachineProfiles::validate(malformed).isEmpty(),"Malformed B head vector is rejected");
    check(profiles.current().value("limits").toObject().value("Z").toArray().at(0).toDouble()==-450,"CM600 lower Z");
    check(profiles.selectProfile("table-y"),"Select 4X Y");
    check(profiles.stockAxisDirection()==QVector3D(0,1,0),"4X Y direction");
    check(profiles.postForAxes(5).isEmpty(),"4X cannot choose 5X post");
    for(const auto v:MachineProfiles::postCatalog()){
        const auto p=v.toObject();
        if(p.value("axes").toInt()==4)check(p.value("words").toString()=="XYZA","4X catalog words exclude B");
    }
    Stock stock; stock.expansion=5;
    check(stock.low()==QVector3D(-5,-55,-55)&&stock.high()==QVector3D(205,55,55),"Expansion applies equally to both extrema");
    check(std::abs(stock.radius({},QVector3D(1,0,0),false)-std::hypot(55.0,55.0))<1e-8,"Box R");
    stock.shape="cylinder";
    check(std::abs(stock.radius({0,5,0},QVector3D(1,0,0),false)-60)<1e-8,"Offset cylinder R");
    stock.shape="frustum";stock.endRatio=0.5;
    check(std::abs(stock.radius({},QVector3D(1,0,0),false)-55)<1e-8,"Frustum R uses large end");
    Workflow workflow;
    workflow.setMachine(4,[&](int axes){return profiles.postForAxes(axes);});
    workflow.setStock(210,110,110,55,105);
    workflow.preset(2);
    auto plan=workflow.snapshot();
    check(plan.value("axes").toInt()==4&&plan.value("post").toString()=="mach3","4X auto-select");
    Workflow restored;
    restored.setMachine(4,[&](int axes){return profiles.postForAxes(axes);});
    check(restored.restore(plan),"Workspace workflow round trip");
    check(restored.toolNames()==workflow.toolNames()&&restored.pathNames()==workflow.pathNames(),"Workspace draft retains tools and paths");
    const auto paths=plan.value("paths").toArray();
    check(paths.size()==4,"Two raster cuts, semi-finish, finish");
    for(const auto item:paths)check(item.toObject().value("minimumRetract").toDouble()==105,"Retract in every path");
    return 0;
}
