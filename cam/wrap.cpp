#include "geometry.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace cam {
namespace {
using P=std::array<double,2>;
double turn(P a,P b,P c){return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);}
std::vector<P> hull(std::vector<P> p){
    std::sort(p.begin(),p.end()); p.erase(std::unique(p.begin(),p.end()),p.end());
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
    h.pop_back();return h;
}
}
Wrap Wrap::fit(const Tree& model,unsigned columns,unsigned rows,double offset){
    Wrap w{std::clamp(columns,8u,96u),std::clamp(rows,4u,128u),{}};
    auto bounds=model.bounds();
    if(bounds.hi.x-bounds.lo.x<1e-8)throw std::invalid_argument("Mẫu không đủ chiều dài để dựng mặt bọc.");
    w.control.resize(w.around*w.along);
    for(unsigned j=0;j<w.along;++j){
        const double t=double(j)/(w.along-1);
        const double x=bounds.lo.x+(bounds.hi.x-bounds.lo.x)*std::clamp(t,1e-7,1-1e-7);
        auto h=hull(model.section(x));
        if(h.size()<3){
            if(j==0){
                const double y=bounds.lo.y,z=bounds.lo.z,y1=bounds.hi.y,z1=bounds.hi.z;
                h={{y,z},{y1,z},{y1,z1},{y,z1}};
            }else{
                for(unsigned i=0;i<w.around;++i){auto p=w.control[(j-1)*w.around+i];p.x=x;w.control[j*w.around+i]=p;}
                continue;
            }
        }
        P center{};for(auto p:h){center[0]+=p[0]/h.size();center[1]+=p[1]/h.size();}
        for(unsigned i=0;i<w.around;++i){
            double a=2*std::numbers::pi*i/w.around;
            P d{std::cos(a),std::sin(a)};double r=0;
            for(size_t k=0;k<h.size();++k){
                P p=h[k],q=h[(k+1)%h.size()];
                double ey=q[0]-p[0],ez=q[1]-p[1],det=d[0]*ez-d[1]*ey;
                if(std::abs(det)<1e-13)continue;
                double ay=p[0]-center[0],az=p[1]-center[1],s=(ay*d[1]-az*d[0])/det;
                if(s>=-1e-9&&s<=1+1e-9)r=std::max(r,(ay*ez-az*ey)/det);
            }
            w.control[j*w.around+i]={x,center[0]+(r+offset)*d[0],center[1]+(r+offset)*d[1]};
        }
    }
    for(double factor:{.42,-.45}){
        auto old=w.control;
        for(unsigned j=1;j+1<w.along;++j)for(unsigned i=0;i<w.around;++i){
            unsigned k=j*w.around+i;
            auto n=(old[j*w.around+(i+1)%w.around]+old[j*w.around+(i+w.around-1)%w.around]+old[k-w.around]+old[k+w.around])*.25;
            auto displacement=(n-old[k])*factor;displacement.x=0;
            w.control[k]=old[k]+displacement;
        }
    }
    return w;
}
V Wrap::eval(double u,double v,V* normal)const{
    u=(u-std::floor(u))*around;
    double t=u-std::floor(u),s=1-t;
    const double b[4]={s*s*s/6,(4-6*t*t+3*t*t*t)/6,(1+3*t+3*t*t-3*t*t*t)/6,t*t*t/6};
    const double d[4]={-s*s/2,(-12*t+9*t*t)/6,(3+6*t-9*t*t)/6,t*t/2};
    std::array<double,133> knots{}, a{}, prev{};
    for(unsigned i=0;i<along+4;++i)knots[i]=std::clamp((double(i)-3)/(along-3),0.,1.);
    v=std::clamp(v,0.,std::nextafter(1.,0.));
    for(unsigned i=0;i<along+3;++i)a[i]=(v>=knots[i]&&v<knots[i+1])?1.:0.;
    for(unsigned p=1;p<=3;++p){
        prev=a;a.fill(0);
        for(unsigned j=0;j<along+3-p;++j){
            const double l=knots[j+p]-knots[j],r=knots[j+p+1]-knots[j+1];
            if(l)a[j]+=(v-knots[j])*prev[j]/l;
            if(r)a[j]+=(knots[j+p+1]-v)*prev[j+1]/r;
        }
    }
    V point{},du{},dv{};
    for(unsigned j=0;j<along;++j){
        const double l=knots[j+3]-knots[j],r=knots[j+4]-knots[j+1];
        double deriv=(l?3*prev[j]/l:0)-(r?3*prev[j+1]/r:0);
        for(unsigned i=0;i<4;++i){
            const auto p=control[j*around+(unsigned(u)+i+around-1)%around];
            point=point+p*(b[i]*a[j]);du=du+p*(d[i]*a[j]*around);dv=dv+p*(b[i]*deriv);
        }
    }
    if(normal)*normal=unit(cross(du,dv));
    return point;
}
}
