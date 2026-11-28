#include "geometry.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>
#include <stdexcept>

namespace cam {
namespace {
using P=std::array<double,2>;
constexpr double tau=2*std::numbers::pi;
double turn(P a,P b,P c){return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);}
std::vector<P> hull(std::vector<P> p){
    std::sort(p.begin(),p.end());
    p.erase(std::unique(p.begin(),p.end()),p.end());
    if(p.size()<3)return {};
    std::vector<P> h;
    for(auto q:p){
        while(h.size()>=2&&turn(h[h.size()-2],h.back(),q)<=0)h.pop_back();
        h.push_back(q);
    }
    const auto lower=h.size();
    for(auto i=p.rbegin()+1;i!=p.rend();++i){
        while(h.size()>lower&&turn(h[h.size()-2],h.back(),*i)<=0)h.pop_back();
        h.push_back(*i);
    }
    h.pop_back();
    return h;
}

double knot(unsigned i,unsigned count){return std::clamp((double(i)-3)/(count-3),0.,1.);}
double greville(unsigned i,unsigned count){return (knot(i+1,count)+knot(i+2,count)+knot(i+3,count))/3;}

struct Section {
    P center{};
    std::vector<double> radius;
};

std::optional<Section> radialSection(std::vector<P> points,unsigned columns,double offset){
    const auto boundary=hull(std::move(points));
    if(boundary.size()<3)return std::nullopt;
    const auto reference=boundary.front();
    double areaTwice=0;
    P centroid{};
    for(size_t i=1;i+1<boundary.size();++i){
        const auto p=boundary[i],q=boundary[i+1];
        const double a=turn(reference,p,q);
        areaTwice+=a;
        centroid[0]+=a*(p[0]+q[0]-2*reference[0]);
        centroid[1]+=a*(p[1]+q[1]-2*reference[1]);
    }
    if(!(areaTwice>0))return std::nullopt;
    Section section{{reference[0]+centroid[0]/(3*areaTwice),reference[1]+centroid[1]/(3*areaTwice)},
                    std::vector<double>(columns)};
    struct Support {double y,z,distance;};
    std::vector<Support> supports;
    supports.reserve(boundary.size());
    for(size_t i=0;i<boundary.size();++i){
        const auto p=boundary[i],q=boundary[(i+1)%boundary.size()];
        const double edgeY=q[0]-p[0],edgeZ=q[1]-p[1];
        const double length=std::hypot(edgeY,edgeZ);
        if(length<=0)continue;
        const double ny=edgeZ/length,nz=-edgeY/length;
        supports.push_back({ny,nz,ny*(p[0]-section.center[0])+nz*(p[1]-section.center[1])+offset});
    }
    for(unsigned column=0;column<columns;++column){
        const double theta=tau*column/columns;
        const double cy=std::cos(theta),sz=std::sin(theta);
        double radius=std::numeric_limits<double>::infinity();
        for(const auto& h:supports){
            const double towards=cy*h.y+sz*h.z;
            if(towards>1e-12)radius=std::min(radius,h.distance/towards);
        }
        if(!(radius>0)||!std::isfinite(radius))return std::nullopt;
        section.radius[column]=radius;
    }
    return section;
}

template<unsigned Degree>
std::array<double,Degree+1> localBasis(unsigned span,double v,unsigned count){
    std::array<double,Degree+1> basis{1},left{},right{};
    for(unsigned p=1;p<=Degree;++p){
        left[p]=v-knot(span+1-p,count);
        right[p]=knot(span+p,count)-v;
        double carry=0;
        for(unsigned i=0;i<p;++i){
            const double denominator=right[i+1]+left[p-i];
            const double scaled=denominator>0?basis[i]/denominator:0;
            basis[i]=carry+right[i+1]*scaled;
            carry=left[p-i]*scaled;
        }
        basis[p]=carry;
    }
    return basis;
}
}

Wrap Wrap::fit(const Tree& model,unsigned columns,unsigned rows,double offset,bool axisY,V pivot){
    if(!std::isfinite(offset)||offset<0)throw std::invalid_argument("Khoảng bù mặt dẫn không hợp lệ.");
    Wrap w{std::clamp(columns,8u,96u),std::clamp(rows,4u,128u),{},axisY,pivot};
    const auto bounds=model.bounds();
    const double first=axisY?bounds.lo.y:bounds.lo.x,last=axisY?bounds.hi.y:bounds.hi.x;
    if(last-first<1e-8)throw std::invalid_argument("Mẫu không đủ chiều dài theo trục quay để dựng mặt dẫn.");

    std::vector<std::optional<Section>> sections(w.along);
    for(unsigned row=0;row<w.along;++row){
        const double v=greville(row,w.along);
        const double axial=first+(last-first)*v;
        sections[row]=radialSection(model.section(axial,axisY),w.around,offset);
        if(!sections[row]&&(row==0||row+1==w.along)){
            const double inward=row==0?1e-7:1-1e-7;
            sections[row]=radialSection(model.section(first+(last-first)*inward,axisY),w.around,offset);
        }
    }
    auto earliest=std::find_if(sections.begin(),sections.end(),[](const auto& row){return row.has_value();});
    if(earliest==sections.end()){
        // A narrow group of triangles may be between the fitted knot stations.
        // Probe more planes before declaring that no real cross-section exists.
        for(unsigned i=1;i<4*w.along&&earliest==sections.end();++i){
            double v=double(i)/(4*w.along);
            auto section=radialSection(model.section(first+(last-first)*v,axisY),w.around,offset);
            if(section){
                const auto row=unsigned(std::round(v*(w.along-1)));
                sections[row]=std::move(section);
                earliest=sections.begin()+row;
            }
        }
    }
    if(earliest==sections.end())throw std::invalid_argument("STL không có tiết diện bao quanh trục quay.");
    std::fill(sections.begin(),earliest,*earliest);
    for(unsigned row=unsigned(earliest-sections.begin())+1;row<w.along;++row){
        if(sections[row])continue;
        auto next=std::find_if(sections.begin()+row+1,sections.end(),[](const auto& item){return item.has_value();});
        const auto start=row-1;
        if(next==sections.end()){
            std::fill(sections.begin()+row,sections.end(),sections[start]);
            break;
        }
        const auto end=unsigned(next-sections.begin());
        for(;row<end;++row){
            const auto t=(greville(row,w.along)-greville(start,w.along))/(greville(end,w.along)-greville(start,w.along));
            Section sample=sections[start].value();
            for(unsigned c=0;c<2;++c)sample.center[c]=std::lerp(sample.center[c],sections[end]->center[c],t);
            for(unsigned c=0;c<w.around;++c)sample.radius[c]=std::lerp(sample.radius[c],sections[end]->radius[c],t);
            sections[row]=std::move(sample);
        }
    }

    std::vector<double> lower,radii;
    lower.reserve(w.around*w.along);
    for(const auto& section:sections)lower.insert(lower.end(),section->radius.begin(),section->radius.end());
    radii=lower;
    for(int iteration=0;iteration<3;++iteration){
        for(double factor:{.50,-.53}){
            const auto source=radii;
            for(unsigned row=0;row<w.along;++row)for(unsigned col=0;col<w.around;++col){
                const auto index=row*w.around+col;
                double sum=source[row*w.around+(col+1)%w.around]+source[row*w.around+(col+w.around-1)%w.around];
                unsigned neighbors=2;
                if(row>0){sum+=source[index-w.around];++neighbors;}
                if(row+1<w.along){sum+=source[index+w.around];++neighbors;}
                radii[index]=source[index]+factor*(sum/neighbors-source[index]);
            }
        }
        for(size_t i=0;i<radii.size();++i)radii[i]=std::max(radii[i],lower[i]);
    }
    // The periodic cubic pulls a uniform control ring toward its centre.
    const double compensation=3/(2+std::cos(tau/w.around));
    const V localOrigin=face(pivot,0,axisY);
    w.control.reserve(radii.size());
    for(unsigned row=0;row<w.along;++row){
        const auto& section=*sections[row];
        const double x=first+(last-first)*greville(row,w.along)-localOrigin.x;
        for(unsigned col=0;col<w.around;++col){
            const double theta=tau*col/w.around;
            const double radius=radii[row*w.around+col]*compensation;
            w.control.push_back({x,section.center[0]-localOrigin.y+radius*std::cos(theta),
                                  section.center[1]-localOrigin.z+radius*std::sin(theta)});
        }
    }
    return w;
}
double Wrap::axialParameter(double axial)const{
    return std::clamp((axial-control.front().x)/(control.back().x-control.front().x),0.,1.);
}
V Wrap::eval(double u,double v,V* normal)const{
    u=(u-std::floor(u))*around;
    const double t=u-std::floor(u),s=1-t;
    const double circum[4]={s*s*s/6,(4-6*t*t+3*t*t*t)/6,(1+3*t+3*t*t-3*t*t*t)/6,t*t*t/6};
    const double circumDerivative[4]={-s*s/2,(-12*t+9*t*t)/6,(3+6*t-9*t*t)/6,t*t/2};
    v=std::clamp(v,0.,1.);
    const auto span=std::min(along-1,3+unsigned(v*(along-3)));
    const auto axial=localBasis<3>(span,v,along);
    const auto quadratic=localBasis<2>(span,v,along);
    V point{},du{},dv{};
    for(unsigned row=0;row<4;++row){
        const auto j=span-3+row;
        const double l=knot(j+3,along)-knot(j,along);
        const double r=knot(j+4,along)-knot(j+1,along);
        const double derivative=(row>0&&l>0?3*quadratic[row-1]/l:0)
                               -(row<3&&r>0?3*quadratic[row]/r:0);
        for(unsigned col=0;col<4;++col){
            const auto p=control[j*around+(unsigned(u)+col+around-1)%around];
            point=point+p*(circum[col]*axial[row]);
            if(normal){
                du=du+p*(circumDerivative[col]*axial[row]*around);
                dv=dv+p*(circum[col]*derivative);
            }
        }
    }
    if(normal)*normal=world(unit(cross(du,dv)),0,axisY);
    return world(point,0,axisY,pivot);
}
}
