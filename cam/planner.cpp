#include "geometry.h"
#include <algorithm>
#include <cmath>
#include <future>
#include <thread>
#include <atomic>
#include <numbers>
#include <stdexcept>

namespace cam {
namespace {
constexpr double inf=std::numeric_limits<double>::infinity();
double sq(double x){return x*x;}
double component(V p,int i){return i==0?p.x:i==1?p.y:p.z;}
double radialBox(V p,V c,double half){return std::hypot(std::abs(p.x-c.x)+half,std::abs(p.y-c.y)+half);}
struct Cell { V p; bool solid; };

class Residual {
    std::vector<std::vector<Cell>> columns_;
    double pitch_,first_;
    bool axisY_;
public:
    double half;
    explicit Residual(const StockSpec& s):axisY_(s.axisY){
        const auto d=s.bounds.hi-s.bounds.lo;
        pitch_=std::max({1.,std::cbrt(d.x*d.y*d.z/115000.),std::max({d.x,d.y,d.z})/400});
        half=pitch_/2;
        first_=component(s.bounds.lo,axisY_)+half;
        int nx=int(std::ceil(d.x/pitch_)),ny=int(std::ceil(d.y/pitch_)),nz=int(std::ceil(d.z/pitch_));
        columns_.resize(axisY_?ny:nx);
        const auto center=(s.bounds.lo+s.bounds.hi)*.5;
        const double transverse=axisY_?d.x:d.y;
        for(int i=0;i<nx;++i)for(int j=0;j<ny;++j)for(int k=0;k<nz;++k){
            V p=s.bounds.lo+V{i*pitch_+half,j*pitch_+half,k*pitch_+half};
            if(s.shape){
                auto q=p-center;
                double f=std::clamp((component(p,axisY_)-half-component(s.bounds.lo,axisY_))/component(d,axisY_),0.,1.);
                double scale=s.shape==2?1-f*(1-s.endRatio):1.;
                if(sq(std::max(0.,std::abs(component(q,!axisY_))-half)/(transverse*.5))+
                   sq(std::max(0.,std::abs(q.z)-half)/(d.z*.5))>sq(scale))continue;
            }
            columns_[axisY_?j:i].push_back({p,true});
        }
    }
    template<class F>
    void visit(V tip,double angle,V pivot,double radius,F callback){
        const double axial=tip.x+component(pivot,axisY_);
        int lo=std::max(0,int(std::floor((axial-radius-half-first_)/pitch_)));
        int hi=std::min(int(columns_.size())-1,int(std::ceil((axial+radius+half-first_)/pitch_)));
        for(int i=lo;i<=hi;++i)for(auto& c:columns_[i]){
            if(!c.solid)continue;
            V p=face(c.p,angle,axisY_,pivot);
            if(std::hypot(std::max(0.,std::abs(p.x-tip.x)-half),std::max(0.,std::abs(p.y-tip.y)-half*1.415))>radius)continue;
            callback(p,c);
        }
    }
    double top(V tip,double angle,V pivot,double radius){
        double z=-inf;
        visit(tip,angle,pivot,radius,[&](V p,const Cell&){z=std::max(z,p.z+half*1.415);});
        return z;
    }
    bool clear(V tip,double angle,V pivot,Span span){
        if(span.high<=span.low)return true;
        bool ok=true;
        visit(tip,angle,pivot,span.radius,[&](V p,const Cell&){
            double pad=half*1.415;
            if(p.z+pad>=tip.z+span.low)ok=false;
        });
        return ok;
    }
    void remove(V tip,double angle,V pivot,const Cutter& tool){
        const double radius=tool.ball>0?tool.ball:tool.diameter/2;
        // Only discard cells fully contained in the cutting volume swept during
        // the vertical plunge. Partial cells remain obstacles for later passes.
        visit(tip,angle,pivot,radius,[&](V p,Cell& cell){
            const double reach=radialBox(p,tip,half*1.415);
            if(reach>=radius)return;
            const double surface=tool.ball>0?tool.ball-std::sqrt(std::max(0.,sq(tool.ball)-sq(reach))):0.;
            if(p.z-half*1.415>tip.z+surface)cell.solid=false;
        });
    }
};
}
double Cutter::profileHeight(double radius)const{
    if(ball<=0)return 0.;
    const double alpha=halfAngle*std::numbers::pi/180.;
    const double join=ball*std::cos(alpha);
    if(radius<=join)return ball-std::sqrt(std::max(0.,ball*ball-radius*radius));
    return ball*(1-std::sin(alpha))+(radius-join)/std::max(.000001,std::tan(alpha));
}
std::array<Span,4> Cutter::spans()const {
    double neck=std::min(flute,profileHeight(diameter*.5));
    return {Span{0,ball,ball>0?ball:diameter*.5},Span{std::min(ball,neck),neck,diameter*.5},
            Span{flute,overhang,diameter*.5},Span{overhang,overhang+40,holderRadius}};
}
double DistanceGrid::lowerBound(V p)const{
    auto q=(p-box.lo)*(1/pitch);
    if(q.x<0||q.y<0||q.z<0||q.x>size[0]-1||q.y>size[1]-1||q.z>size[2]-1)return 0;
    int x=int(std::round(q.x)),y=int(std::round(q.y)),z=int(std::round(q.z));
    const auto d=std::abs(samples[(size_t(z)*size[1]+y)*size[0]+x]);
    auto delta=p-(box.lo+V{x*pitch,y*pitch,z*pitch});
    return std::max(0.,double(d)-std::sqrt(dot(delta,delta))-std::max(1.,double(d))*2e-7);
}
DistanceGrid DistanceGrid::measure(const Tree& model,Bounds b,double pitch){
    if(!std::isfinite(pitch)||pitch<=0)throw std::invalid_argument("Kích thước ô khoảng cách không hợp lệ.");
    auto span=b.hi-b.lo;
    DistanceGrid out{b,pitch,{},{}};size_t n=1;
    for(int a=0;a<3;++a){
        auto steps=std::ceil(component(span,a)/pitch);
        if(steps<0||steps>250||n*size_t(steps+1)>2'000'000)throw std::invalid_argument("Lưới khoảng cách vượt giới hạn.");
        out.size[a]=int(steps)+1;n*=size_t(steps+1);
    }
    out.samples.resize(n);
    const size_t workers=std::min<size_t>(std::max(1u,std::thread::hardware_concurrency()),(n+255)/256);
    std::atomic_size_t next{0};
    auto run=[&]{
        constexpr size_t block=64;
        for(size_t begin;(begin=next.fetch_add(block,std::memory_order_relaxed))<n;){
            for(size_t index=begin;index<std::min(begin+block,n);++index){
                auto layer=index/out.size[0];
                V point=b.lo+V{(index%out.size[0])*pitch,(layer%out.size[1])*pitch,(layer/out.size[1])*pitch};
                out.samples[index]=float(model.distance(point)*(model.insideClosedMesh(point)?-1:1));
            }
        }
    };
    {
        std::vector<std::jthread> jobs;
        for(size_t i=1;i<workers;++i)jobs.emplace_back(run);
        run();
    }
    return out;
}

Plan generate(const std::vector<Tri>& mesh,const StockSpec& stock,const std::vector<Cutter>& tools,
              const std::vector<PathSpec>& paths,bool twoFace,V pivot,double safe,const RasterProgress& progress){
    Plan plan{{},0,safe};
    const double transverse=stock.axisY?pivot.x:pivot.y;
    if(std::abs(transverse*1000-std::round(transverse*1000))>1e-7)
        throw std::invalid_argument("Tâm ngang cần khớp độ phân giải NC 0,001 mm để giữ đúng đường tâm.");
    Residual residual(stock);
    Tree model(mesh);
    for(size_t pass=0;pass<paths.size();++pass){
        const auto p=paths[pass];const auto& tool=tools.at(size_t(p.tool));
        if(p.step<.01||tool.diameter<=0||tool.overhang<tool.flute||tool.flute<=0||tool.ball>tool.diameter*.5||tool.halfAngle<0||tool.halfAngle>=89||tool.holderRadius<tool.diameter*.5)
            throw std::invalid_argument("Thông số dao hoặc bước dịch không hợp lệ.");
        double localLow=inf,localHigh=-inf;
        auto localStock=[&](double angle){
            V low{inf,inf,inf},high{-inf,-inf,-inf};
            for(int mask=0;mask<8;++mask){
                auto b=stock.bounds;
                V x=face({mask&1?b.hi.x:b.lo.x,mask&2?b.hi.y:b.lo.y,mask&4?b.hi.z:b.lo.z},angle,stock.axisY,pivot);
                low={std::min(low.x,x.x),std::min(low.y,x.y),std::min(low.z,x.z)};
                high={std::max(high.x,x.x),std::max(high.y,x.y),std::max(high.z,x.z)};
            }
            return Bounds{low,high};
        };
        auto initial=localStock(0);
        // Model both end clamps as reserved axial stock zones. The full
        // assembly footprint stays outside these zones.
        double keep=2.+tool.holderRadius;
        localLow=std::max(initial.lo.x+keep,p.clipMin);localHigh=std::min(initial.hi.x-keep,p.clipMax);
        if(localHigh<=localLow)throw std::invalid_argument("Không đủ chiều dài giữa hai vùng giữ phôi.");
        const double requestedAngles=std::ceil(2*std::numbers::pi*std::max(1.,safe-50)/p.step);
        if(p.angle<0&&requestedAngles>4000)throw std::runtime_error("Bước dịch yêu cầu hơn 4.000 góc phân độ. Chia vùng hoặc chỉnh bước.");
        const int angles=p.angle>=0?1:std::max(16,int(requestedAngles));
        auto roundMm=[](double value){return std::round(value*1000)/1000;};
        for(int k=0;k<angles;++k){
            double angle=p.angle>=0?p.angle:roundMm(double(k)*360/angles);
            auto box=localStock(angle);
            model.setFrame(angle,stock.axisY,pivot);
            const double tipRadius=tool.ball>0?tool.ball:tool.diameter*.5;
            const auto spans=tool.spans();
            const int lengthSteps=std::max(1,int(std::ceil((localHigh-localLow)/(p.pointSpacing>0?p.pointSpacing:p.step))));
            const int widthSteps=p.angle<0?0:std::max(1,int(std::ceil((box.hi.y-box.lo.y)/p.step)));
            const double bottom=std::max({twoFace?-1.:1.,box.hi.z-tool.overhang+1.,p.minZ});
            if(size_t(lengthSteps+1)*size_t(widthSteps+1)>1'500'000)throw std::runtime_error("Bước dịch tạo quá nhiều điểm cho một mặt.");
            if(progress)progress(pass,0,size_t(widthSteps)+1);
            for(int j=0;j<=widthSteps;++j){
                if(progress&&(j%4==0||j==widthSteps))progress(pass,size_t(j)+1,size_t(widthSteps)+1);
                double y=p.angle<0?0.:box.lo.y+(box.hi.y-box.lo.y)*j/widthSteps;
                for(int i=0;i<=lengthSteps;++i){
                    double x=localLow+(localHigh-localLow)*((bool(j%2)!=p.reverse)?lengthSteps-i:i)/lengthSteps;
                    auto quantized=world({x,y,0},0,stock.axisY,pivot);
                    quantized.x=roundMm(quantized.x);
                    quantized.y=roundMm(quantized.y);
                    auto query=face(quantized,0,stock.axisY,pivot);
                    x=query.x;const double queryY=p.angle<0?0.:query.y;
                    const double drop=model.drop(x,queryY,tipRadius,tool.ball>0);
                    if(p.angle<0&&!std::isfinite(drop))continue;
                    // Expand the footprint and add axial allowance conservatively.
                    const double allowed=p.allowance>0?
                        model.drop(x,queryY,tipRadius+p.allowance,tool.ball>0)+p.allowance:drop;
                    double z=std::max(allowed,drop)+.002;
                    if(p.angle>=0)z=std::max(z,bottom);
                    if(!std::isfinite(z)||z>p.maxZ||z>=box.hi.z-.001)continue;
                    V point{x,queryY,z};
                    auto clear=[&](V q){
                        if(tool.ball>0&&tool.halfAngle>0){
                            // Cone/flute envelope as coaxial conservative rings.
                            constexpr int bands=12;
                            for(int band=0;band<bands;++band){
                                double a=tool.ball*std::cos(tool.halfAngle*std::numbers::pi/180.);
                                double r0=a+(tool.diameter*.5-a)*band/bands;
                                double r1=a+(tool.diameter*.5-a)*(band+1)/bands;
                                double low=tool.profileHeight(r0);
                                if(low<tool.flute&&!model.spanClear(q,{low,tool.flute,r1}))return false;
                            }
                            const double top=tool.profileHeight(tool.diameter*.5);
                            if(top<tool.flute&&!model.spanClear(q,{top,tool.flute,tool.diameter*.5}))return false;
                        }else if(tool.ball>0&&tool.ball<tool.flute&&
                                 !model.spanClear(q,{tool.ball,tool.flute,tool.diameter*.5}))return false;
                        return model.spanClear(q,spans[2])&&model.spanClear(q,spans[3])&&
                               residual.clear(q,angle,pivot,spans[2])&&residual.clear(q,angle,pivot,spans[3]);
                    };
                    double level=std::max(z,std::min(std::min(p.maxZ,box.hi.z),residual.top(point,angle,pivot,tool.diameter*.5)));
                    do {
                        level=std::max(z,level-std::min(p.stepDown>0?p.stepDown:2.,tool.flute*.3));point.z=level;
                        if(!clear(point)){++plan.rejected;break;}
                        bool link=false;
                        if(tool.ball==0&&!plan.cuts.empty()){
                            const auto& prev=plan.cuts.back();
                            const auto delta=point-prev.point, middle=(point+prev.point)*.5;
                            const double gap=std::hypot(delta.x,delta.y);
                            if(prev.pass==int(pass)&&prev.angle==angle&&std::abs(delta.z)<1e-10&&gap<=p.step*1.001&&std::abs(delta.y)<1e-10){
                                const Span shank{spans[2].low,spans[2].high,spans[2].radius+gap*.5};
                                const Span holder{spans[3].low,spans[3].high,spans[3].radius+gap*.5};
                                // Enclose the swept footprint on this level by a
                                // disk at its midpoint. Fall back to retract if
                                // that conservative enclosure touches material.
                                link=model.height(middle.x,middle.y,tipRadius+gap*.5)<middle.z-.0015&&
                                     model.spanClear(middle,shank,.0015)&&model.spanClear(middle,holder,.0015)&&
                                     residual.clear(middle,angle,pivot,shank)&&residual.clear(middle,angle,pivot,holder);
                            }
                        }
                        plan.cuts.push_back({point,angle,p.tool,int(pass),link});
                        residual.remove(point,angle,pivot,tool);
                    }while(level>z+1e-8);
                }
            }
        }
    }
    return plan;
}
}
