#include "cam/motion.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <string_view>

int main(int argc,char** argv){
    using namespace cam;
    const bool stress=argc==2&&std::string_view(argv[1])=="--stress";
    constexpr double length=250,pitch=.22;
    const double radius=stress?.02:3;
    const int sides=stress?8:48;
    std::vector<Tri> mesh;
    for(int i=0;i<sides;++i){
        const auto rim=[=](int side){
            const double t=side*2*std::numbers::pi/sides;
            return V{0,radius*std::sin(t),radius*std::cos(t)};
        };
        const V a=rim(i),b=rim(i+1),end{length,0,0};
        for(int slice=0;slice<50;++slice){
            const V low{5.*slice,0,0},high{5.*(slice+1),0,0};
            mesh.push_back({{a+low,a+high,b+high}});
            mesh.push_back({{a+low,b+high,b+low}});
        }
        mesh.push_back({{{},a,b}});
        mesh.push_back({{end,b+end,a+end}});
    }
    Tree tree(std::move(mesh));
    Machine machine;
    machine.safe=stress?50.04:54;
    const Cutter cutter=stress?Cutter{.008,.004,0,16,12,.02}:Cutter{.8,.4,0,16,12,1};
    PathSpec spec{0,-1,pitch,0,true};
    spec.pointSpacing=stress?.00135:.065;
    size_t completed=0,expected=0,perTurn=0;
    const auto start=std::chrono::steady_clock::now();
    const auto plan=planSpiral(tree,machine,cutter,spec,0,{},nullptr,[&](size_t done,size_t total,size_t per){
        if(done<completed)std::abort();
        completed=done;expected=total;perTurn=per;
    });
    double first=0,last=0;
    size_t cuts=0,backward=0;
    for(const auto& motion:plan.moves){
        if(motion.kind!=MotionKind::Cut)continue;
        if(cuts==0)first=motion.pose.a;
        else if(motion.pose.a<last)++backward;
        last=motion.pose.a;
        ++cuts;
    }
    const double turns=(last-first)/360.;
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"250 mm, pitch "<<pitch<<" mm, "<<completed<<" layers, "<<perTurn
             <<" points/turn, "<<turns<<" revolutions, "<<cuts<<" cuts, "
             <<plan.lifts.size()<<" lifts, "<<plan.unreachable<<" unreachable, "
             <<seconds<<" s\n";
    const size_t threshold=stress?5'000'000:400'000;
    return completed==expected&&completed>threshold&&cuts>threshold&&turns>1100&&
           backward==0&&plan.lifts.empty()&&plan.unreachable==0?0:1;
}
