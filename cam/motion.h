#pragma once
#include "geometry.h"
#include <optional>
#include <string>
#include <string_view>
#include <functional>

namespace cam {
struct Pose {V tip;double a=0,b=0;}; // tip in instantaneous table frame
struct Machine {
    bool axisY=false;
    int axes=4,aSign=1,bSign=1;
    V pivot{},machineOffset{};
    double arm=0,safe=60;
    std::array<std::optional<std::array<double,2>>,5> limits{};
    enum Dialect{Plain,Tcp434,Traori,Direct} dialect=Plain;
};
enum class MotionKind {Cut,Approach,Rapid};
struct Motion {
    Pose pose;
    int tool=0,pass=0;
    MotionKind kind=MotionKind::Cut;
    unsigned sourceLine=0;
};
struct Lift {int pass;Pose location;std::string reason;};
struct Spiral {std::vector<Motion> moves;std::vector<Lift> lifts;unsigned unreachable=0;};
using GuideNormal=std::function<V(double u,double v)>;
V partPoint(Pose p,const Machine& m);
Pose interpolate(Pose a,Pose b,double t);
Pose controllerPose(Pose a,Pose b,double t,const Machine& m);
double controllerSpeed(Pose a,Pose b,const Machine& m);
V mechanical(Pose p,const Machine& m);
std::string limitIssue(Pose p,const Machine& m);
enum class TipCheck { Strict, PlannedContact, NcRounded };
class MotionChecks {
public:
    MotionChecks(Tree& model,const Machine& machine,const DistanceGrid* sdf=nullptr):model_(model),machine_(machine),sdf_(sdf){}
    double clearance(Pose,const Cutter&) const;
    bool sweep(Pose a,Pose b,const Cutter&,const std::function<Pose(double)>& curve={},double speedBound=0,double minimumZ=-std::numeric_limits<double>::infinity(),TipCheck tip=TipCheck::Strict) const;
private:
    Tree& model_;
    const Machine& machine_;
    const DistanceGrid* sdf_;
};
Spiral planSpiral(Tree&,const Machine&,const Cutter&,const PathSpec&,int pass,const GuideNormal& guide={},const DistanceGrid* sdf=nullptr);
std::string writeMotionNc(const Spiral&,const Machine&,std::string_view post);
struct Diagnostic {unsigned line;std::string message;};
struct Verified {std::vector<Motion> motions;std::vector<Diagnostic> errors;unsigned blocks=0;};
Verified verifyMotionNc(std::string_view,const Machine&,const std::vector<Cutter>&,MotionChecks&);
}
