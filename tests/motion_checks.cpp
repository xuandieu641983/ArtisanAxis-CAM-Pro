#include "cam/motion.h"
#include "cam/stock_sim.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <algorithm>
using namespace cam;
static void check(bool ok,const char* detail){if(!ok){std::cerr<<detail<<'\n';std::exit(1);}}
static std::vector<Tri> cylinder(double length,double r,V origin={}){
    std::vector<Tri> mesh;
    for(int i=0;i<80;++i){
        double a=i*2*std::numbers::pi/80,b=(i+1)*2*std::numbers::pi/80;
        V p=origin+V{0,r*std::sin(a),r*std::cos(a)};
        V q=origin+V{0,r*std::sin(b),r*std::cos(b)};
        V d{length,0,0};
        mesh.push_back({{p,p+d,q+d}});mesh.push_back({{p,q+d,q}});
        mesh.push_back({{origin,p,q}});mesh.push_back({{origin+d,q+d,p+d}});
    }
    return mesh;
}
static bool hasCuts(const Spiral& p){return std::any_of(p.moves.begin(),p.moves.end(),[](const Motion& m){return m.kind==MotionKind::Cut;});}
static void printErrors(const Verified& result){
    for(size_t i=0;i<std::min(size_t(8),result.errors.size());++i)
        std::cerr<<"line "<<result.errors[i].line<<": "<<result.errors[i].message<<'\n';
}
int main(){
    const Cutter ball{2,1,0,18,12,2.5};
    auto body=cylinder(19,6);
    Tree mesh(body);
    Machine four;four.safe=58;
    four.limits[1]=std::array{1.0,5.};
    auto offsetGuide=[](double u,double v){
        double a=(.25-u)*2*std::numbers::pi, lateral=.24*std::sin(6.283185307179586*v);
        return unit(V{0,std::sin(a)+lateral*std::cos(a),std::cos(a)-lateral*std::sin(a)});
    };
    auto run=planSpiral(mesh,four,ball,{0,-1,3.,0.,true},0,offsetGuide);
    check(hasCuts(run),"Off-center spiral feed missing");
    double a0=0,a1=0,loY=1000,hiY=-1000;size_t feed=0,y=0;
    for(const auto& m:run.moves)if(m.kind==MotionKind::Cut){
        if(!feed)a0=m.pose.a;
        a1=m.pose.a;loY=std::min(loY,m.pose.tip.y);hiY=std::max(hiY,m.pose.tip.y);++feed;y+=std::abs(m.pose.tip.y)>1e-6;
        check(m.pose.b==0,"B is locked in 4X");
        mesh.setFrame(m.pose.a,false,{});
        const double expected=mesh.drop(m.pose.tip.x,m.pose.tip.y,ball.ball,true);
        check(std::abs(expected-m.pose.tip.z)<1e-9,"Finish point is not exactly on STL offset envelope");
    }
    check(a1-a0>360&&y==feed&&hiY-loY>.2&&run.lifts.empty(),"Spiral should accumulate A off-center without intermediate lift");
    auto nc4=writeMotionNc(run,four,"shanlong4");check(nc4.find(" B")==std::string::npos,"B output on 4X");
    MotionChecks check4(mesh,four);
    auto ver4=verifyMotionNc(nc4,four,{ball},check4);
    printErrors(ver4);check(ver4.errors.empty(),"4X generated NC round trip");
    std::cout<<"4X: "<<feed<<" linked feed moves, "<<y<<" off-center, A span "<<a1-a0<<" deg, Y ["<<loY<<", "<<hiY<<"], "<<run.lifts.size()<<" intermediate lifts\n";
    {
        // The center solid lies above the cutter spindle during the crossing.
        Tree obstacle(cylinder(3,1.0,{-1.5,0,28}));
        Machine axis;MotionChecks checkMotion(obstacle,axis);
        Pose p{{0,-7,0},0,0},q{{0,7,0},0,0};
        check(checkMotion.clearance(p,ball)>=0&&checkMotion.clearance(q,ball)>=0,"Endpoints must be clear");
        SweepFailure failure=SweepFailure::None;
        check(!checkMotion.sweep(p,q,ball,{},0,-INFINITY,TipCheck::Strict,&failure)&&failure==SweepFailure::Assembly,
              "A middle-only collet collision was missed or misclassified");
        Pose left{{0,0,0},0,-15},right{{0,0,0},0,15};
        check(checkMotion.clearance(left,ball)>=0&&checkMotion.clearance(right,ball)>=0,"Tilt endpoints must be clear");
        check(!checkMotion.sweep(left,right,ball),"Middle-only collet collision while tilting was missed");
        std::cout<<"Swept fixtures: middle-only collet collision caught on both translation and B reversal\n";
    }
    Machine five;five.axes=5;five.dialect=Machine::Tcp434;five.arm=232.918;five.safe=62;
    five.machineOffset={0,0,-105};five.limits[2]=std::array{-450.,200.};five.limits[4]=std::array{-20.,20.};
    {
        auto eccentric=five;
        const V lever{78.7,0,232.9175};
        eccentric.headPivotFromTip=lever;
        check(dot(headCompensation(0,eccentric),headCompensation(0,eccentric))<1e-20,
              "Neutral head eccentricity must preserve the measured datum");
        const auto plus=headCompensation(20,eccentric);
        const auto minus=headCompensation(-20,eccentric);
        check(std::abs((minus.z-plus.z)-2*lever.x*std::sin(std::numbers::pi/9))<1e-9&&
              std::abs(headBendRadius(eccentric)-std::hypot(lever.x,lever.z))<1e-9,
              "A signed lateral pivot offset must produce asymmetric mechanical Z travel");
    }
    auto arms=cylinder(19,5);
    for(double yArm:{-7.2,7.2}){
        auto arm=cylinder(13,.85,{3,yArm,.5});
        arms.insert(arms.end(),arm.begin(),arm.end());
    }
    Tree gaps(arms);
    auto grid=DistanceGrid::measure(gaps,{{0,-10,-8},{19,10,9}},2);
    auto guide=[](double u,double){
        const double theta=(.25-u)*2*std::numbers::pi;
        return unit(V{.20,std::sin(theta),std::cos(theta)});
    };
    Cutter slim{1,.5,0,18,14,1};
    auto p5=planSpiral(gaps,five,slim,{0,-1,3.,0.,true},0,guide,&grid);
    check(hasCuts(p5),"5X feed missing on separation fixture");
    size_t usedB=0,gapContacts=0;
    for(const auto& move:p5.moves)if(move.kind==MotionKind::Cut){
        usedB+=std::abs(move.pose.b)>.1;
        auto center=partPoint(move.pose,five);
        gapContacts+=std::abs(center.y)>5.1&&std::abs(center.y)<6.85&&center.x>4&&center.x<15;
    }
    std::cout<<"5X split fixture: "<<p5.moves.size()<<" moves, "<<usedB<<" linked tilted points, "<<gapContacts<<" near-gap points, "
             <<p5.lifts.size()<<" intermediate lifts, "<<p5.unreachable<<" rejected layers\n";
    check(usedB>0&&gapContacts>0,"5X fixture must have tilted motion and points in the measured gap strip");
    for(bool rtcp:{true,false}){
        auto m=five;m.dialect=rtcp?Machine::Tcp434:Machine::Direct;
        MotionChecks verifier(gaps,m,&grid);
        auto use=rtcp?p5:planSpiral(gaps,m,slim,{0,-1,3.,0.,true},0,guide,&grid);
        auto program=writeMotionNc(use,m,rtcp?"cm600":"non-rtcp");
        auto result=verifyMotionNc(program,m,{slim},verifier);
        printErrors(result);check(result.errors.empty(),"5X NC round trip");
        check((program.find("G43.4 H1")!=std::string::npos)==rtcp,"RTCP enable");
        if(rtcp)check(program.ends_with("G49\nM5\nM30\n"),"RTCP cancellation order");
        auto corrupt=m;corrupt.machineOffset.z=10000;
        MotionChecks invalid(gaps,corrupt,&grid);
        auto faults=verifyMotionNc(program,corrupt,{slim},invalid);
        check(!faults.errors.empty()&&faults.errors.front().line>0,"Z limits must produce a line diagnosis");
        check(result.motions.size()>2,"Round trip must include linkage");
        auto endpoint=result.motions.back().pose;
        check(std::abs(endpoint.tip.z-(m.safe+.42))<.001,"Reconstructed endpoint");
        std::cout<<(rtcp?"CM600":"Direct")<<": "<<result.blocks<<" motion blocks, 0 verifier errors; Z offset corruption caught\n";
    }
    {
        auto m=five;m.dialect=Machine::Traori;MotionChecks checkSiemens(gaps,m,&grid);
        auto bytes=writeMotionNc(p5,m,"traori");
        auto result=verifyMotionNc(bytes,m,{slim},checkSiemens);
        check(bytes.find("TRAORI\n")!=std::string::npos&&bytes.find("TRAFOOF\n")!=std::string::npos&&result.errors.empty(),
              "TRAORI model round trip");
    }
    {
        const auto afterSeam=partToolAxis({{},365,10},five);
        const auto beforeSeam=partToolAxis({{},355,10},five);
        const auto middle=slerpToolAxis(beforeSeam,afterSeam,.5);
        check(std::abs(dot(middle,middle)-1)<1e-12&&
              std::abs(dot(middle,beforeSeam)-dot(middle,afterSeam))<1e-12&&
              dot(middle,partToolAxis({{},360,10},five))>.99999,"SLERP follows the short arc through the A seam");
        Tree cylinderModel(cylinder(11,3));
        const auto wrap=Wrap::fit(cylinderModel,12,8,1);
        const DriveGuide sample=[&](double u,double x){
            DriveSample s;s.point=wrap.eval(u,wrap.axialParameter(x),&s.normal);
            return s;
        };
        auto machine=five;
        machine.safe=55;
        machine.bSign=-1;
        machine.limits[4]=std::array{-7.,12.};
        PathSpec path{0,-1,2.,0.,true};
        path.lead=9.;path.lean=10.;
        auto run=planSpiral(cylinderModel,machine,slim,path,0,{},nullptr,{},sample);
        check(hasCuts(run),"Guide lead/lean fixture produced no cutting motion");
        bool biased=false;
        double previous=-1e9;
        for(const auto& move:run.moves){
            if(move.kind!=MotionKind::Cut)continue;
            const auto p=move.pose;
            check(p.a>=previous&&p.b>=-12&&p.b<=7,"5X head respects mapped B limits and continuous A");
            previous=p.a;
            biased|=p.b<-.5;
            cylinderModel.setFrame(p.a,machine.axisY,machine.pivot,p.b);
            const auto q=tilt(p.tip,-p.b);
            check(std::abs(cylinderModel.drop(q.x,q.y,slim.ball,true)-q.z)<1e-8,
                  "Lead/lean finish point must be reprojected to exact STL contact");
        }
        check(biased,"Requested lean must be reflected in at least one admissible head angle");
        MotionChecks checkMachine(cylinderModel,machine);
        auto roundTrip=verifyMotionNc(writeMotionNc(run,machine,"cm600"),machine,{slim},checkMachine);
        printErrors(roundTrip);
        check(roundTrip.errors.empty(),"SLERP/reprojected drive-surface path passed the controller sweep");
        std::cout<<"Lead/Lean: "<<roundTrip.blocks<<" verified blocks with inverted, asymmetric B limits\n";
    }
    StockSpec stock{{{0,-7,-7},{19,7,7}},0,1,false};
    Spiral stockJob;stockJob.moves={{{{8,0,15},0,0},0,0,MotionKind::Rapid},{{{8,0,5.7},0,0},0,0,MotionKind::Approach},
        {{{9,0,5.6},0,0},0,1,MotionKind::Cut},{{{10,0,5.5},0,0},0,2,MotionKind::Cut}};
    auto sim=simulateStock(stock,stockJob,four,{ball},{0,1,2},mesh);
    check(sim.stages.size()==3&&sim.stages[0].remaining>=sim.stages[1].remaining&&
          sim.stages[1].remaining>=sim.stages[2].remaining&&sim.stages[0].remaining>sim.stages[2].remaining,
          "Stock state must evolve over three stages");
    std::cout<<"Stock voxels: "<<sim.stages[0].remaining<<" / "<<sim.stages[1].remaining<<" / "<<sim.stages[2].remaining<<'\n';
    Spiral withdrawal;
    withdrawal.moves={{{{8,0,5.5},0,0},0,0,MotionKind::Approach},
                      {{{8,0,four.safe+.2},0,0},0,0,MotionKind::Retract}};
    auto withdrawStock=simulateStock(stock,withdrawal,four,{ball},{0},mesh);
    check(withdrawStock.stockWarnings==1&&withdrawStock.stages.size()==1&&withdrawStock.stages[0].gouge==0,
          "A retract crossing residual stock must warn without treating it as a cut");
    check(std::any_of(ver4.motions.begin(),ver4.motions.end(),[](const Motion& m){return m.kind==MotionKind::Rapid;}),
          "4X rapid is retained");
}
