#include "geometry.h"
#include <algorithm>
#include <cmath>
#include <future>
#include <thread>
#include <atomic>
#include <numbers>
#include <stdexcept>
#include <limits>

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
    std::vector<std::vector<V>> framed_;
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
    void setFrame(double angle,V pivot){
        framed_.resize(columns_.size());
        for(size_t i=0;i<columns_.size();++i){
            auto& framed=framed_[i];
            framed.resize(columns_[i].size());
            for(size_t j=0;j<framed.size();++j)framed[j]=face(columns_[i][j].p,angle,axisY_,pivot);
        }
    }
    template<class F>
    void visit(V tip,V pivot,double radius,F callback){
        const double axial=tip.x+component(pivot,axisY_);
        int lo=std::max(0,int(std::floor((axial-radius-half-first_)/pitch_)));
        int hi=std::min(int(columns_.size())-1,int(std::ceil((axial+radius+half-first_)/pitch_)));
        const double radiusSquared=radius*radius;
        for(int i=lo;i<=hi;++i)for(size_t j=0;j<columns_[i].size();++j){
            auto& c=columns_[i][j];
            if(!c.solid)continue;
            V p=framed_[i][j];
            if(sq(std::max(0.,std::abs(p.x-tip.x)-half))+sq(std::max(0.,std::abs(p.y-tip.y)-half*1.415))>radiusSquared)continue;
            if(!callback(p,c))return;
        }
    }
    double top(V tip,V pivot,double radius){
        double z=-inf;
        visit(tip,pivot,radius,[&](V p,const Cell&){z=std::max(z,p.z+half*1.415);return true;});
        return z;
    }
    bool clear(V tip,V pivot,Span span){
        if(span.high<=span.low)return true;
        bool ok=true;
        visit(tip,pivot,span.radius,[&](V p,const Cell&){
            ok=p.z+half*1.415<tip.z+span.low;
            return ok;
        });
        return ok;
    }
    void remove(V tip,V pivot,const Cutter& tool){
        const double radius=tool.ball>0?tool.ball:tool.diameter/2;
        // Only discard cells fully contained in the cutting volume swept during
        // the vertical plunge. Partial cells remain obstacles for later passes.
        visit(tip,pivot,radius,[&](V p,Cell& cell){
            const double reach=radialBox(p,tip,half*1.415);
            if(reach>=radius)return true;
            const double surface=tool.ball>0?tool.ball-std::sqrt(std::max(0.,sq(tool.ball)-sq(reach))):0.;
            if(p.z-half*1.415>tip.z+surface)cell.solid=false;
            return true;
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
    // This is a lower bound on the distance to solid material. An interior
    // signed sample must never certify clearance merely because its
    // distance to the nearest boundary is large.
    const auto d=samples[(size_t(z)*size[1]+y)*size[0]+x];
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
            residual.setFrame(angle,pivot);
            const double tipRadius=tool.ball>0?tool.ball:tool.diameter*.5;
            const auto spans=tool.spans();
            const int lengthSteps=std::max(1,int(std::ceil((localHigh-localLow)/(p.pointSpacing>0?p.pointSpacing:p.step))));
            const int widthSteps=p.angle<0?0:std::max(1,int(std::ceil((box.hi.y-box.lo.y)/p.step)));
            const double bottom=std::max({twoFace?-1.:1.,box.hi.z-tool.overhang+1.,p.minZ});
            if(size_t(lengthSteps+1)*size_t(widthSteps+1)>1'500'000)throw std::runtime_error("Bước dịch tạo quá nhiều điểm cho một mặt.");
            const double stepDown=std::min(p.stepDown>0?p.stepDown:2.,tool.flute*.3);
            auto contact=[&](const Tree& tree,int j,int i){
                const double y=p.angle<0?0.:box.lo.y+(box.hi.y-box.lo.y)*j/widthSteps;
                const double x=localLow+(localHigh-localLow)*((bool(j%2)!=p.reverse)?lengthSteps-i:i)/lengthSteps;
                auto quantized=world({x,y,0},0,stock.axisY,pivot);
                quantized.x=roundMm(quantized.x);
                quantized.y=roundMm(quantized.y);
                const auto query=face(quantized,0,stock.axisY,pivot);
                const double queryY=p.angle<0?0.:query.y;
                const double drop=tree.drop(query.x,queryY,tipRadius,tool.ball>0);
                if(p.angle<0&&!std::isfinite(drop))return V{query.x,queryY,inf};
                const double allowed=p.allowance>0?
                    tree.drop(query.x,queryY,tipRadius+p.allowance,tool.ball>0)+p.allowance:drop;
                double z=std::max(allowed,drop)+.002;
                if(p.angle>=0)z=std::max(z,bottom);
                return V{query.x,queryY,z};
            };
            auto clear=[&](V q){
                if(tool.ball>0&&tool.halfAngle>0){
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
                       residual.clear(q,pivot,spans[2])&&residual.clear(q,pivot,spans[3]);
            };
            auto emit=[&](V point){
                if(!clear(point)){++plan.rejected;return false;}
                bool link=false;
                if(tool.ball==0&&!plan.cuts.empty()){
                    const auto& prev=plan.cuts.back();
                    const auto delta=point-prev.point, middle=(point+prev.point)*.5;
                    const double gap=std::hypot(delta.x,delta.y);
                    if(prev.pass==int(pass)&&prev.angle==angle&&std::abs(delta.z)<1e-10&&gap<=p.step*1.001&&std::abs(delta.y)<1e-10){
                        const Span shank{spans[2].low,spans[2].high,spans[2].radius+gap*.5};
                        const Span holder{spans[3].low,spans[3].high,spans[3].radius+gap*.5};
                        // Enclose the swept footprint in a disk at the middle
                        // of the segment, or retract when the proof fails.
                        link=model.height(middle.x,middle.y,tipRadius+gap*.5)<middle.z-.0015&&
                             model.spanClear(middle,shank,.0015)&&model.spanClear(middle,holder,.0015)&&
                             residual.clear(middle,pivot,shank)&&residual.clear(middle,pivot,holder);
                    }
                }
                plan.cuts.push_back({point,angle,p.tool,int(pass),link});
                residual.remove(point,pivot,tool);
                return true;
            };
            if(p.angle>=0&&!twoFace){
                // Evaluate the STL once at every XY station. At each common Z
                // level cut only stations where the expanded model envelope
                // permits that entire cutter footprint.
                const size_t columns=size_t(lengthSteps)+1, rows=size_t(widthSteps)+1;
                std::vector<V> envelope(columns*rows);
                std::atomic_size_t nextRow{0};
                auto calculate=[&]{
                    const Tree query=model;
                    for(size_t row;(row=nextRow.fetch_add(1,std::memory_order_relaxed))<rows;)
                        for(size_t col=0;col<columns;++col)envelope[row*columns+col]=contact(query,int(row),int(col));
                };
                {
                    std::vector<std::jthread> workers;
                    const auto count=std::min<size_t>(std::max(1u,std::thread::hardware_concurrency()),(envelope.size()+255)/256);
                    for(size_t thread=1;thread<count;++thread)workers.emplace_back(calculate);
                    calculate();
                }
                const double top=box.hi.z;
                if(top<=bottom||p.maxZ<bottom)continue;
                // Equalize the layers so they end at the allowed floor.
                // The requested stepdown is a strict upper bound.
                const double levelCount=std::floor((top-bottom)/stepDown)+1;
                if(levelCount>1'000'000 || levelCount*double(envelope.size())>150'000'000)
                    throw std::runtime_error("Chia lớp Z tạo quá nhiều điểm cho một mặt.");
                const auto levels=size_t(levelCount);
                if(progress)progress(pass,0,levels);
                for(size_t layer=1;layer<=levels;++layer){
                    const double level=layer==levels?bottom:std::lerp(top,bottom,double(layer)/levels);
                    if(level<=p.maxZ){
                        for(const auto& floor:envelope){
                            if(!std::isfinite(floor.z)||floor.z>level+1e-9)continue;
                            const V point{floor.x,floor.y,level};
                            const auto occupiedTop=residual.top(point,pivot,tipRadius);
                            if(occupiedTop<=level)continue;
                            // A Z cap cannot request an abrupt deep entry into
                            // intact stock above the first allowed plane.
                            if(p.maxZ<top-stepDown&&occupiedTop>level+stepDown+residual.half*2){
                                ++plan.rejected;continue;
                            }
                            emit(point);
                        }
                    }
                    if(progress)progress(pass,layer,levels);
                }
                continue;
            }
            if(progress)progress(pass,0,size_t(widthSteps)+1);
            for(int j=0;j<=widthSteps;++j){
                for(int i=0;i<=lengthSteps;++i){
                    V point=contact(model,j,i);
                    const double z=point.z;
                    if(!std::isfinite(z)||z>p.maxZ||z>=box.hi.z-.001)continue;
                    double level=std::max(z,std::min(std::min(p.maxZ,box.hi.z),residual.top(point,pivot,tool.diameter*.5)));
                    do {
                        level=std::max(z,level-stepDown);point.z=level;
                        if(!emit(point))break;
                    }while(level>z+1e-8);
                }
                if(progress&&(j%4==0||j==widthSteps))progress(pass,size_t(j)+1,size_t(widthSteps)+1);
            }
        }
    }
    return plan;
}
}
