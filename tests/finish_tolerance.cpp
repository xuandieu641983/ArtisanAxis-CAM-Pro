#include "cam/motion.h"
#include <iostream>
using namespace cam;

int main(){
    Tree plane({{{{-4,-4,0},{4,-4,0},{4,4,0}}},{{{-4,-4,0},{4,4,0},{-4,4,0}}}});
    Machine m;
    const Cutter t{1,.5,0,10,6,2};
    MotionChecks checks(plane,m);
    auto rounded=[&](double z){
        Spiral p;
        p.moves={
            {{{0,0,m.safe},0,0},0,0,MotionKind::Rapid},
            {{{0,0,z},0,0},0,0,MotionKind::Approach},
            {{{.25,0,z},0,0},0,0,MotionKind::Cut},
            {{{.25,0,m.safe},0,0},0,0,MotionKind::Rapid}
        };
        return verifyMotionNc(writeMotionNc(p,m,"shanlong4"),m,{t},checks);
    };
    if(!rounded(-.003).errors.empty()||rounded(-.006).errors.empty()){
        std::cerr<<"Rounded NC tip tolerance exceeded its contract\n";return 1;
    }
    Tree fixture({{{{1.2,-.1,10.001},{1.4,-.1,10.001},{1.3,.1,10.001}}}});
    MotionChecks guard(fixture,m);
    const Pose at{{0,0,0},0,0};
    if(guard.sweep(at,at,t,{},0,-std::numeric_limits<double>::infinity(),TipCheck::NcRounded)){
        std::cerr<<"NC verifier incorrectly applied tool-tip tolerance to collet\n";return 1;
    }
    std::cout<<"Accepted 0.003 mm tip discrepancy; rejected 0.006 mm tip discrepancy and 0.001 mm collet interference\n";
}
