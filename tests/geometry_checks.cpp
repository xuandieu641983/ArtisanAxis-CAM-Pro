#include "cam/geometry.h"
#include "cam/nc.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <regex>
#include <sstream>
using namespace cam;

static void check(bool ok,const char* detail){
    if(!ok){std::cerr<<detail<<'\n';std::exit(1);}
}
static double norm(V p){return std::sqrt(dot(p,p));}
static std::vector<Tri> cube(V lo,V hi){
    V p[8];for(int i=0;i<8;++i)p[i]={i&1?hi.x:lo.x,i&2?hi.y:lo.y,i&4?hi.z:lo.z};
    const int face[6][4]={{0,4,6,2},{1,3,7,5},{0,1,5,4},{2,6,7,3},{0,2,3,1},{4,5,7,6}};
    std::vector<Tri> mesh;
    for(auto& q:face){mesh.push_back({{p[q[0]],p[q[1]],p[q[2]]}});mesh.push_back({{p[q[0]],p[q[2]],p[q[3]]}});}
    return mesh;
}
int main(){
    for(bool axisY:{false,true})for(int angle:{0,90,180,270}){
        double error=0;
        for(V p: {V{23,12,-8},V{-7,1,6},V{3,4,5},V{-21,-13,21}}){
            V pivot{2,-3,4};
            error=std::max(error,norm(world(face(p,angle,axisY,pivot),angle,axisY,pivot)-p));
        }
        std::cout<<"Round trip "<<(axisY?"Y":"X")<<' '<<angle<<": "<<std::fixed;
        std::cout.precision(3);std::cout<<error<<" mm\n";
        check(error==0,"Quarter-turn round trip");
        const auto a=face({1,0,0},angle,axisY),b=face({0,1,0},angle,axisY),c=face({0,0,1},angle,axisY);
        check(dot(cross(a,b),c)==1,"No mirrored frame");
    }
    check(face({0,1,0},90).z==1&&face({0,1,0},270).z==-1,"Known 90/270 direction");
    const double R=.4,slope=.45;const auto n=unit({-slope,0,1});
    const Tri inclined{{{-20,-20,-20*slope},{20,-20,20*slope},{0,30,0}}};
    const auto tip=ballTip({},n,{0,0,1},R);
    const double planeError=std::abs(triangleDrop(inclined,tip.x,tip.y,R,true)-tip.z);
    check(planeError<1e-12,"Inclined single compensation");
    check(std::abs(triangleDrop(inclined,0,0,R,false)-slope*R)<1e-12,"Flat disk inclined face");
    const Tri peak{{{0,0,10},{2,0,9},{0,2,9}}};
    check(std::abs(triangleDrop(peak,0,0,R,true)-10)<1e-12,"Convex crest drop");
    // Tessellate a 5 mm sphere into patches and compare 0.4 mm ball drop
    // against its analytical (spherical Minkowski sum) result.
    std::vector<Tri> spherical;
    auto sphere=[](double u,double v){
        const double a=u*6.283185307179586,b=v*3.141592653589793;
        return V{5*std::sin(b)*std::cos(a),5*std::sin(b)*std::sin(a),5*std::cos(b)};
    };
    for(int i=0;i<120;++i)for(int j=0;j<80;++j){
        V a=sphere(i/120.,j/80.),b=sphere((i+1)/120.,j/80.),c=sphere((i+1)/120.,(j+1)/80.),d=sphere(i/120.,(j+1)/80.);
        spherical.push_back({{a,b,c}});spherical.push_back({{a,c,d}});
    }
    Tree ball(spherical);
    double sphereError=0;
    for(auto p:{V{0,0,0},V{1.6,.7,0},V{-2.,1.2,0},V{2.8,-.4,0}}){
        auto exact=std::sqrt((5+R)*(5+R)-p.x*p.x-p.y*p.y)-R;
        sphereError=std::max(sphereError,std::abs(ball.drop(p.x,p.y,R,true)-exact));
    }
    check(sphereError<.004,"Spherical STL drop error");
    std::cout.precision(9);
    std::cout<<"Inclined error: "<<planeError<<" mm; sphere max error: "<<sphereError<<" mm\n";
    const auto solid=cube({0,-2,-2},{30,2,2});Tree shape(solid);
    auto sdf=DistanceGrid::measure(shape,{{14,-3,-3},{16,3,3}},1.);
    check(sdf.samples.at((3*7+3)*3+1)<-1.99f&&sdf.samples.front()>0,"Signed distances on closed solid");
    auto wrap=Wrap::fit(shape,20,12,1);
    V normal,otherNormal;auto w=wrap.eval(0,.4,&normal),w1=wrap.eval(1,.4,&otherNormal);
    check(norm(w-w1)<1e-12&&norm(normal-otherNormal)<1e-12&&std::abs(norm(normal)-1)<1e-12,"Periodic seam and normal");
    check(std::abs(wrap.eval(.4,0).x-0)<1e-5&&std::abs(wrap.eval(.4,1).x-30)<1e-5,"Clamped axial ends");
    int falseAccepts=0,falseRejects=0;
    std::array<Span,4> spans{{{0,1,.8},{1,3,1.5},{3,8,2},{8,15,4}}};
    for(const auto span:spans){
        Tree obstacle(cube({-.2,-.2,span.low+.1},{.2,.2,span.low+.2}));
        falseAccepts+=obstacle.spanClear({0,0,0},span);
        falseRejects+=!obstacle.spanClear({15,0,0},span);
    }
    std::cout<<"4-Span fixture: "<<falseAccepts<<" colliding poses accepted, "<<falseRejects<<" clear poses rejected\n";
    check(!falseAccepts&&!falseRejects,"Four envelope collision fixtures");
    const std::vector<Cutter> tools{{4,0,0,10,5,3},{2,1,0,12,5,3}};
    const StockSpec stock{{{0,-4,-4},{30,4,4}},0,.5,false};
    auto plan=generate(solid,stock,tools,{{0,0,2,.5,false},{0,180,2,.5,false},{1,-1,3,0,true}},true,{},56);
    check(!plan.cuts.empty(),"2-face and rotary plan generated");
    bool indexed=false;
    for(const auto cut:plan.cuts)if(cut.pass==2){indexed=true;check(cut.point.y==0,"Rotary transverse centerline");}
    check(indexed,"Rotary cut generated");
    auto four=generate(solid,stock,tools,{{0,0,2,.5,false},{0,180,2,.5,false},{0,90,2,.5,false},{0,270,2,.5,false}},false,{},56);
    for(int pass:{0,1,2,3})check(std::any_of(four.cuts.begin(),four.cuts.end(),[&](Cut p){return p.pass==pass;}),"Every raster face generated");
    const auto narrow=cube({0,-1,-1},{30,1,1});
    const StockSpec wide{{{0,-7,-4},{30,7,4}},0,.5,false};
    auto longTools=tools;longTools[0].flute=8;
    auto floor2=generate(narrow,wide,longTools,{{0,0,2,.5,false}},true,{},59);
    check(std::any_of(floor2.cuts.begin(),floor2.cuts.end(),[](Cut c){return c.point.z==-1;}),"2-face can reach minus one when clear");
    auto floor4=generate(narrow,wide,longTools,{{0,0,2,.5,false}},false,{},59);
    check(!floor4.cuts.empty()&&std::all_of(floor4.cuts.begin(),floor4.cuts.end(),[](Cut c){return c.point.z>=1.;}),"4-face preserves center core");
    std::cout<<"Sample 2-face + rotary: "<<plan.cuts.size()<<" moves, "<<plan.rejected<<" rejected; four faces: "<<four.cuts.size()<<" moves\n";
    for(auto post:{"shanlong4","jd45","jd50","mach3","weihong4","syntec4","richauto"}){
        const auto nc=writeNc(plan,post,false,{},1);
        check(nc.starts_with("(XUAN DIEU - huynhgiaphattc@gmail.com)\n"),"Header exact");
        check(!std::regex_search(nc,std::regex("(^|[ \\n])B[ +-]*[0-9]")),"No rotary B word");
        check(nc.find("M0")!=std::string::npos&&nc.find("G01 Z")!=std::string::npos&&nc.ends_with("M5\nM30\n"),"Stop/tool and closing commands");
        std::istringstream in(nc);std::string line,previous;
        while(std::getline(in,line)){
            if(line.starts_with("G00 A"))check(previous=="G00 Z56.000","Retract before every A");
            previous=line;
        }
    }
    std::cout<<"NC: 7 profiles passed ASCII header, safe A index, feed, tool stop, and B-word check\n";
}
