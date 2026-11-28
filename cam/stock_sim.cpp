#include "stock_sim.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace cam {
namespace {
double norm(V p){return std::sqrt(dot(p,p));}
double sq(double x){return x*x;}
struct Cell{bool wood=false,goal=false;};
}
StockSim simulateStock(const StockSpec& stock,const Spiral& program,const Machine& machine,const std::vector<Cutter>& tools,
                       const std::vector<int>& groups,const Tree& model){
    const auto lo=stock.bounds.lo,span=stock.bounds.hi-lo,center=lo+span*.5;
    if(!(span.x>0&&span.y>0&&span.z>0))throw std::invalid_argument("Invalid stock bounds");
    const double h=std::max({.25,std::cbrt(span.x*span.y*span.z/100000.),std::max({span.x,span.y,span.z})/180.});
    const int nx=int(std::ceil(span.x/h)),ny=int(std::ceil(span.y/h)),nz=int(std::ceil(span.z/h));
    auto id=[=](int x,int y,int z){return (size_t(z)*ny+y)*nx+x;};
    auto point=[=](int x,int y,int z){return lo+V{(x+.5)*h,(y+.5)*h,(z+.5)*h};};
    std::vector<Cell> cells(size_t(nx)*ny*nz);
    for(int z=0;z<nz;++z)for(int y=0;y<ny;++y)for(int x=0;x<nx;++x){
        V p=point(x,y,z),q=p-center;
        if(p.x>lo.x+span.x||p.y>lo.y+span.y||p.z>lo.z+span.z)continue;
        if(stock.shape){
            double f=stock.axisY?(p.y-lo.y)/span.y:(p.x-lo.x)/span.x;
            double scale=stock.shape==2?1-f*(1-stock.endRatio):1.;
            double r=stock.axisY?q.x/(span.x*.5):q.y/(span.y*.5);
            if(sq(r)+sq(q.z/(span.z*.5))>sq(scale))continue;
        }
        cells[id(x,y,z)]={true,model.insideClosedMesh(p)};
    }
    StockSim output;output.pitch=h;
    auto occupied=[&](int x,int y,int z){
        return x>=0&&y>=0&&z>=0&&x<nx&&y<ny&&z<nz&&cells[id(x,y,z)].wood;
    };
    auto capture=[&](int group){
        StockStage stage;stage.group=group;
        for(int z=0;z<nz;++z)for(int y=0;y<ny;++y)for(int x=0;x<nx;++x){
            const auto cell=cells[id(x,y,z)];
            stage.remaining+=cell.wood;
            stage.excess+=cell.wood&&!cell.goal;
            stage.gouge+=!cell.wood&&cell.goal;
            if(!cell.wood){if(cell.goal)stage.gougeMap.push_back(point(x,y,z));continue;}
            if(!occupied(x-1,y,z)||!occupied(x+1,y,z)||!occupied(x,y-1,z)||!occupied(x,y+1,z)||
               !occupied(x,y,z-1)||!occupied(x,y,z+1)){
                if(cell.goal)stage.shell.push_back(point(x,y,z));
                else stage.excessMap.push_back(point(x,y,z));
            }
        }
        output.stages.push_back(std::move(stage));
    };
    auto update=[&](Pose p,const Cutter& tool,bool canCut){
        V tip=partPoint(p,machine);
        V end=partPoint({p.tip+tilt({0,0,tool.overhang+40},p.b),p.a,p.b},machine);
        V axis=unit(end-tip);
        double r=tool.holderRadius;
        auto from=[=](double a,double b,double origin){return std::max(0,int(std::floor((std::min(a,b)-r-origin)/h)));};
        auto to=[=](double a,double b,double origin,int n){return std::min(n-1,int(std::floor((std::max(a,b)+r-origin)/h)));};
        bool warning=false;
        int x0=from(tip.x,end.x,lo.x),x1=to(tip.x,end.x,lo.x,nx);
        int y0=from(tip.y,end.y,lo.y),y1=to(tip.y,end.y,lo.y,ny);
        int z0=from(tip.z,end.z,lo.z),z1=to(tip.z,end.z,lo.z,nz);
        for(int z=z0;z<=z1;++z)for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){
            auto& cell=cells[id(x,y,z)];
            if(!cell.wood)continue;
            auto d=point(x,y,z)-tip;
            double q=dot(d,axis), radial=norm(d-axis*q);
            if(q<0||radial>r)continue;
            if(q<tool.flute&&radial<=tool.diameter*.5&&q>=tool.profileHeight(radial)){
                if(canCut)cell.wood=false;
                else warning=true;
            }else if(q>=tool.flute&&q<=tool.overhang+40&&radial<=(q>=tool.overhang?r:tool.diameter*.5))
                warning=true;
        }
        return warning;
    };
    int group=-1;
    for(size_t i=1;i<program.moves.size();++i){
        const auto& move=program.moves[i];auto previous=program.moves[i-1];
        if(move.tool<0||move.pass<0||size_t(move.pass)>=groups.size())continue;
        const auto& tool=tools.at(size_t(move.tool));
        const int nextGroup=groups[size_t(move.pass)];
        if(group!=-1&&nextGroup!=group)capture(group);
        group=nextGroup;
        double travel=controllerSpeed(previous.pose,move.pose,machine)+
            (std::abs(move.pose.a-previous.pose.a)*(std::hypot(move.pose.tip.y,move.pose.tip.z)+tool.overhang+40)+
             std::abs(move.pose.b-previous.pose.b)*(tool.overhang+40))*std::numbers::pi/180.;
        if(!std::isfinite(travel)||travel/h>100000)throw std::runtime_error("Stock simulation movement exceeds sample budget");
        int steps=std::max(1,int(std::ceil(travel/(h*.55))));
        bool warning=false;
        const bool canCut=move.kind==MotionKind::Cut||move.kind==MotionKind::Approach;
        for(int step=1;step<=steps;++step)
            warning|=update(controllerPose(previous.pose,move.pose,double(step)/steps,machine),tool,canCut);
        output.stockWarnings+=warning;
        if(warning)output.warningMoves.push_back(move.sourceLine?move.sourceLine:unsigned(i));
    }
    if(group!=-1)capture(group);
    return output;
}
}
