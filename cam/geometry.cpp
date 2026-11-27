#include "geometry.h"
#include <algorithm>
#include <cmath>
#include <future>
#include <numbers>
#include <stdexcept>
#include <tuple>
namespace cam {
namespace {
constexpr double inf=std::numeric_limits<double>::infinity(), eps=1e-12;
double sq(double a){return a*a;}
double coord(V v,int i){return i==0?v.x:i==1?v.y:v.z;}
Bounds empty(){return {{inf,inf,inf},{-inf,-inf,-inf}};}
void extend(Bounds& a,V v){
    a.lo={std::min(a.lo.x,v.x),std::min(a.lo.y,v.y),std::min(a.lo.z,v.z)};
    a.hi={std::max(a.hi.x,v.x),std::max(a.hi.y,v.y),std::max(a.hi.z,v.z)};
}
double boxDist2(Bounds b,V p){
    auto d=[](double q,double lo,double hi){return std::max({lo-q,q-hi,0.});};
    return sq(d(p.x,b.lo.x,b.hi.x))+sq(d(p.y,b.lo.y,b.hi.y))+sq(d(p.z,b.lo.z,b.hi.z));
}
bool containsXY(const Tri& t,double x,double y){
    auto area=[](V a,V b,V c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);};
    const V q{x,y,0}; const double a=area(t.p[0],t.p[1],t.p[2]);
    if(std::abs(a)<=eps)return false;
    for(int i=0;i<3;++i)if(area(t.p[i],t.p[(i+1)%3],q)*std::copysign(1.,a)<-eps)return false;
    return true;
}
double segmentDist2(V q,V a,V b){
    const auto d=b-a;const double len=dot(d,d);
    return dot(q-a-d*(len>eps?std::clamp(dot(q-a,d)/len,0.,1.):0.),
               q-a-d*(len>eps?std::clamp(dot(q-a,d)/len,0.,1.):0.));
}
double triDist2(V q,const Tri& t){
    const auto n=unit(cross(t.p[1]-t.p[0],t.p[2]-t.p[0]));
    if(dot(n,n)>0){
        const auto h=dot(q-t.p[0],n);const auto p=q-n*h;
        bool inside=true;
        for(int i=0;i<3;++i)inside &= dot(n,cross(t.p[(i+1)%3]-t.p[i],p-t.p[i]))>=-eps;
        if(inside)return h*h;
    }
    return std::min({segmentDist2(q,t.p[0],t.p[1]),segmentDist2(q,t.p[1],t.p[2]),segmentDist2(q,t.p[2],t.p[0])});
}
std::array<double,2> rotation(double a){
    a=std::remainder(a,360.);
    if(a==0)return {1,0};
    if(a==90)return {0,1};
    if(a==-90)return {0,-1};
    if(std::abs(a)==180)return {-1,0};
    a*=std::numbers::pi/180;return {std::cos(a),std::sin(a)};
}
}
double dot(V a,V b){return a.x*b.x+a.y*b.y+a.z*b.z;}
V cross(V a,V b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
V unit(V a){const double len=std::sqrt(dot(a,a));return len>eps?a*(1/len):V{};}
V face(V p,double a,bool y,V origin){
    p=p-origin;
    if(y)p={p.y,-p.x,p.z};
    auto [c,s]=rotation(a);return {p.x,c*p.y-s*p.z,s*p.y+c*p.z};
}
V world(V p,double a,bool y,V origin){
    auto [c,s]=rotation(a);V q{p.x,c*p.y+s*p.z,-s*p.y+c*p.z};
    if(y)q={-q.y,q.x,q.z};
    return q+origin;
}
V tilt(V p,double a){auto [c,s]=rotation(a);return {c*p.x+s*p.z,p.y,-s*p.x+c*p.z};}
V ballTip(V p,V n,V u,double r){return p+(n-u)*r;}

double triangleDrop(const Tri& t,double x,double y,double r,bool spherical){
    double z=-inf;
    auto consider=[&](V p){
        double d2=sq(p.x-x)+sq(p.y-y);
        if(d2<=r*r+1e-10)z=std::max(z,p.z+(spherical?std::sqrt(std::max(0.,r*r-d2))-r:0.));
    };
    for(auto p:t.p)consider(p);
    const auto n=unit(cross(t.p[1]-t.p[0],t.p[2]-t.p[0]));
    if(std::abs(n.z)>eps){
        auto norm=n.z>0?n:n*(-1);
        double px=x,py=y;
        const double h=std::hypot(norm.x,norm.y);
        if(spherical){px-=r*norm.x;py-=r*norm.y;}
        else if(h>eps){px-=r*norm.x/h;py-=r*norm.y/h;}
        if(containsXY(t,px,py))consider({px,py,t.p[0].z-(norm.x*(px-t.p[0].x)+norm.y*(py-t.p[0].y))/norm.z});
    }
    for(int i=0;i<3;++i){
        auto a=t.p[i], d=t.p[(i+1)%3]-a;
        const double speed=std::hypot(d.x,d.y);
        if(speed<=eps)continue;
        const double t0=((x-a.x)*d.x+(y-a.y)*d.y)/sq(speed);
        const V nearest=a+d*t0;
        double rad2=r*r-sq(nearest.x-x)-sq(nearest.y-y);
        if(rad2<-eps)continue;
        rad2=std::max(rad2,0.);
        const double reach=std::sqrt(rad2)/speed;
        const double left=std::max(0.,t0-reach), right=std::min(1.,t0+reach);
        if(left>right)continue;
        const double opt=spherical?t0+reach*d.z/std::hypot(speed,d.z):(d.z>=0?right:left);
        consider(a+d*std::clamp(opt,left,right));
    }
    return z;
}

Tree::Tree(std::vector<Tri> mesh):tris_(std::move(mesh)){
    if(tris_.empty())throw std::invalid_argument("STL không có mặt tam giác.");
    // Heap indices permit disjoint, parallel writes through the upper levels.
    const size_t n=tris_.size();
    size_t leaves=1;while(leaves<(n+7)/8)leaves*=2;
    nodes_.resize(2*leaves);
    build(0,unsigned(n),1);
}
unsigned Tree::build(unsigned first,unsigned last,unsigned idx){
    auto& n=nodes_[idx];n={empty(),first,last,0,0};
    for(unsigned i=first;i<last;++i)for(auto v:tris_[i].p)extend(n.box,v);
    if(last-first<=8)return idx;
    const auto span=n.box.hi-n.box.lo;int axis=span.x>=span.y&&span.x>=span.z?0:span.y>=span.z?1:2;
    const unsigned mid=first+(last-first)/2;
    auto centroid=[axis](const Tri& t){return coord(t.p[0]+t.p[1]+t.p[2],axis);};
    std::nth_element(tris_.begin()+first,tris_.begin()+mid,tris_.begin()+last,[&](auto& a,auto& b){return centroid(a)<centroid(b);});
    n.left=idx*2;n.right=idx*2+1;
    if(idx<4 && last-first>32000){
        auto left=std::async(std::launch::async,[&]{build(first,mid,n.left);});
        build(mid,last,n.right);left.get();
    }else{build(first,mid,n.left);build(mid,last,n.right);}
    return idx;
}
Bounds Tree::bounds()const{return nodes_[1].box;}
void Tree::setFrame(double angle,bool axisY,V pivot,double b){
    angle_=angle;axisY_=axisY;pivot_=pivot;framed_=true;
    basis_[0]=tilt(face({1,0,0},angle_,axisY_),-b);
    basis_[1]=tilt(face({0,1,0},angle_,axisY_),-b);
    basis_[2]=tilt(face({0,0,1},angle_,axisY_),-b);
}
V Tree::inFrame(V p)const{
    p=p-pivot_;return basis_[0]*p.x+basis_[1]*p.y+basis_[2]*p.z;
}
Bounds Tree::inFrame(Bounds b)const{
    auto c=inFrame((b.lo+b.hi)*.5), h=(b.hi-b.lo)*.5;
    V radius;
    for(int i=0;i<3;++i){
        auto q=basis_[i];double v=coord(h,i);
        radius=radius+V{std::abs(q.x),std::abs(q.y),std::abs(q.z)}*v;
    }
    return {c-radius,c+radius};
}
double Tree::drop(double x,double y,double r,bool spherical)const{
    double best=-inf;
    unsigned pending[128]={1};int count=1;
    while(count){
        const auto& n=nodes_[pending[--count]];
        const auto b=framed_?inFrame(n.box):n.box;
        const double dx=std::max({b.lo.x-x,0.,x-b.hi.x}),dy=std::max({b.lo.y-y,0.,y-b.hi.y});
        if(sq(dx)+sq(dy)>r*r+eps || b.hi.z<best)continue;
        if(n.left){pending[count++]=n.left;pending[count++]=n.right;}
        else for(unsigned i=n.begin;i<n.end;++i){
            auto t=tris_[i];if(framed_)for(auto& p:t.p)p=inFrame(p);
            best=std::max(best,triangleDrop(t,x,y,r,spherical));
        }
    }
    return best;
}
bool Tree::spanClear(V tip,Span s,double tolerance)const{
    if(s.high<=s.low)return true;
    // Conservative: all target geometry in this projected disk must remain
    // below the bottom of this non-cutting segment.
    return height(tip.x,tip.y,s.radius)<tip.z+s.low-tolerance;
}
double Tree::distance(V p)const{
    double best=inf;unsigned pending[128]={1};int count=1;
    while(count){
        const auto& n=nodes_[pending[--count]];
        if(boxDist2(n.box,p)>best)continue;
        if(n.left){
            const auto a=boxDist2(nodes_[n.left].box,p),b=boxDist2(nodes_[n.right].box,p);
            pending[count++]=a>b?n.left:n.right;pending[count++]=a>b?n.right:n.left;
        }else for(unsigned i=n.begin;i<n.end;++i)best=std::min(best,triDist2(p,tris_[i]));
    }
    return std::sqrt(best);
}
bool Tree::insideClosedMesh(V p)const{
    p.x+=1.23456789e-8;p.y+=2.34567891e-8;
    std::vector<double> hits;
    unsigned pending[128]={1};int count=1;
    while(count){
        const auto& n=nodes_[pending[--count]];
        if(p.x<n.box.lo.x||p.x>n.box.hi.x||p.y<n.box.lo.y||p.y>n.box.hi.y||p.z>=n.box.hi.z)continue;
        if(n.left){pending[count++]=n.left;pending[count++]=n.right;continue;}
        for(unsigned i=n.begin;i<n.end;++i){
            const auto& t=tris_[i];
            const auto norm=cross(t.p[1]-t.p[0],t.p[2]-t.p[0]);
            if(std::abs(norm.z)<eps||!containsXY(t,p.x,p.y))continue;
            double z=t.p[0].z-(norm.x*(p.x-t.p[0].x)+norm.y*(p.y-t.p[0].y))/norm.z;
            if(z>p.z)hits.push_back(z);
        }
    }
    std::sort(hits.begin(),hits.end());
    size_t unique=0;double last=-inf;
    for(double z:hits)if(z-last>1e-7){++unique;last=z;}
    return unique%2!=0;
}
std::vector<std::array<double,2>> Tree::section(double x)const{
    std::vector<std::array<double,2>> out;
    unsigned pending[128]={1};int count=1;
    while(count){
        const auto& n=nodes_[pending[--count]];
        if(x<n.box.lo.x||x>n.box.hi.x)continue;
        if(n.left){pending[count++]=n.left;pending[count++]=n.right;continue;}
        for(unsigned i=n.begin;i<n.end;++i)for(int e=0;e<3;++e){
            V a=tris_[i].p[e],b=tris_[i].p[(e+1)%3];
            if(a.x==x)out.push_back({a.y,a.z});
            if((a.x<x&&b.x>x)||(a.x>x&&b.x<x)){
                V p=a+(b-a)*((x-a.x)/(b.x-a.x));out.push_back({p.y,p.z});
            }
        }
    }
    return out;
}
}
