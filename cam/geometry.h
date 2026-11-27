#pragma once
#include <array>
#include <vector>
#include <limits>
#include <cstddef>
#include <memory>
#include <functional>

namespace cam {
struct V {
    double x=0,y=0,z=0;
    V operator+(V b)const{return {x+b.x,y+b.y,z+b.z};}
    V operator-(V b)const{return {x-b.x,y-b.y,z-b.z};}
    V operator*(double s)const{return {x*s,y*s,z*s};}
};
struct Tri { V p[3]; };
struct Bounds { V lo,hi; };
double dot(V a,V b);
V cross(V a,V b);
V unit(V a);
V face(V world,double degrees,bool axisY=false,V origin={});
V world(V local,double degrees,bool axisY=false,V origin={});
V tilt(V p,double degrees);
V ballTip(V contact,V normal,V axis,double r);
double triangleDrop(const Tri& t,double x,double y,double radius,bool spherical);
struct Span {double low,high,radius;};
class Tree {
public:
    explicit Tree(std::vector<Tri> mesh);
    void setFrame(double angle,bool axisY,V pivot,double b=0);
    double drop(double x,double y,double r,bool spherical) const;
    double height(double x,double y,double r)const{return drop(x,y,r,false);}
    bool spanClear(V tip,Span span,double tolerance=1e-7)const;
    double distance(V p) const;
    bool insideClosedMesh(V p) const;
    Bounds bounds()const;
    std::vector<std::array<double,2>> section(double x)const;
private:
    struct Node { Bounds box; unsigned begin=0,end=0,left=0,right=0; };
    double angle_=0;
    bool axisY_=false, framed_=false;
    V pivot_{}, basis_[3]{};
    V inFrame(V)const;
    Bounds inFrame(Bounds)const;
    struct Storage { std::vector<Tri> tris; std::vector<Node> nodes; };
    // Copies share immutable mesh storage and retain independent query frames.
    std::shared_ptr<Storage> data_;
    unsigned build(unsigned first,unsigned last,unsigned depth);
};
struct Wrap {
    unsigned around=0,along=0;
    std::vector<V> control;
    static Wrap fit(const Tree&,unsigned around,unsigned along,double offset);
    V eval(double u,double v,V* normal=nullptr)const;
};
}

namespace cam {
struct DistanceGrid {
    Bounds box;
    double pitch;
    std::array<int,3> size;
    std::vector<float> samples;
    double lowerBound(V p) const;
    static DistanceGrid measure(const Tree&,Bounds,double pitch);
};
struct Cutter {
    double diameter,ball,halfAngle,overhang,flute,holderRadius;
    double profileHeight(double radius) const;
    std::array<Span,4> spans() const;
};
struct StockSpec {Bounds bounds; int shape;double endRatio;bool axisY;};
struct PathSpec {
    int tool,angle;
    double step,allowance;
    bool finish;
    double pointSpacing=0,stepDown=0;
    double minZ=-std::numeric_limits<double>::infinity(),maxZ=std::numeric_limits<double>::infinity();
    double clipMin=-std::numeric_limits<double>::infinity(),clipMax=std::numeric_limits<double>::infinity();
    double bMin=-20,bMax=20,feed=600;
    int rpm=16000;
    bool reverse=false;
    int axes=0; // 0 inherits the machine/program mode.
};
struct Cut {V point;double angle;int tool;int pass;bool link=false;};
struct Plan {std::vector<Cut> cuts; unsigned rejected=0; double safe;};
using RasterProgress=std::function<void(size_t pass,size_t row,size_t total)>;
Plan generate(const std::vector<Tri>& mesh,const StockSpec&,const std::vector<Cutter>&,
              const std::vector<PathSpec>&,bool twoFace,V pivot,double safe,const RasterProgress& progress={});
}
