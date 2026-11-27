#include "motion.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <exception>

namespace cam {
namespace {
constexpr double pi=std::numbers::pi, inf=std::numeric_limits<double>::infinity();
double norm(V p){return std::sqrt(dot(p,p));}
double rotaryRadius(V p){return std::hypot(p.y,p.z);}
double component(V p,int n){return n==0?p.x:n==1?p.y:p.z;}
V cutterAxis(double b){return tilt({0,0,1},b);}
V swing(double b,double arm){return (cutterAxis(b)-V{0,0,1})*arm;}
bool inside(double v,const std::optional<std::array<double,2>>& bounds,double pad=0){
    return std::isfinite(v)&&(!bounds||(v-pad>=(*bounds)[0]&&v+pad<=(*bounds)[1]));
}
double rotationDistance(double a,double b){return std::abs(a-b)*pi/180.;}
}
V partPoint(Pose p,const Machine& m){return world(p.tip,p.a,m.axisY,m.pivot);}
V mechanical(Pose p,const Machine& m){return world(p.tip+swing(p.b,m.arm),0,m.axisY,m.pivot)+m.machineOffset;}
Pose interpolate(Pose a,Pose b,double t){return {a.tip+(b.tip-a.tip)*t,a.a+(b.a-a.a)*t,a.b+(b.b-a.b)*t};}
std::string limitIssue(Pose p,const Machine& m){
    auto v=mechanical(p,m);
    const char* labels[]={"X","Y","Z","A","B"};
    const double values[]={v.x,v.y,v.z,p.a*m.aSign,p.b*m.bSign};
    for(int i=0;i<5;++i)if(!inside(values[i],m.limits[i]))return std::string("Soft limit ")+labels[i];
    return {};
}

 // An isotropic pad encloses displacement of a rigid cutter assembly in
 // stock space. Cylindrical rings intentionally overbound the tapered spans.
static bool clearWithPad(Tree& tree,const Machine& m,const DistanceGrid* sdf,Pose pose,const Cutter& t,double pad,double tipPad=-1,TipCheck tip=TipCheck::Strict){
    if(tipPad<0)tipPad=pad;
    const V q=tilt(pose.tip,-pose.b);
    tree.setFrame(pose.a,m.axisY,m.pivot,pose.b);
    // Only the NC reader admits a small tip-contact discrepancy. The
    // same allowance never relaxes the cone, body or collet envelope.
    const double tolerance=tip==TipCheck::NcRounded?.004:0.;
    if(tip!=TipCheck::PlannedContact&&t.ball>0){
        const V center=partPoint({pose.tip+cutterAxis(pose.b)*t.ball,pose.a,pose.b},m);
        const auto r=std::max(0.,t.ball-tolerance)+tipPad;
        if((!sdf||sdf->lowerBound(center)<=r)&&tree.distance(center)<=r)return false;
    }else if(tip!=TipCheck::PlannedContact&&t.ball<=0&&
             tree.height(q.x,q.y,t.diameter*.5+tipPad)>=q.z-tipPad+tolerance)return false;
    auto clear=[&](double low,double high,double radius){
        if(high<=low)return true;
        return tree.spanClear(q,{low-pad,high+pad,radius+pad});
    };
    if(t.ball>0&&t.halfAngle>0){
        constexpr int bands=16;
        double join=t.ball*std::cos(t.halfAngle*pi/180.);
        double rMax=t.diameter*.5;
        for(int k=0;k<bands;++k){
            double a=join+(rMax-join)*k/bands,b=join+(rMax-join)*(k+1)/bands;
            auto low=t.profileHeight(a);
            if(low<t.flute&&!clear(low,t.flute,b))return false;
        }
        if(!clear(t.profileHeight(rMax),t.flute,rMax))return false;
    }else if(t.ball>0&&!clear(t.ball,t.flute,t.diameter*.5))return false;
    return clear(t.flute,t.overhang,t.diameter*.5)&&clear(t.overhang,t.overhang+40,t.holderRadius);
}
double MotionChecks::clearance(Pose p,const Cutter& t)const{
    if(t.ball>0&&model_.insideClosedMesh(partPoint({p.tip+cutterAxis(p.b)*t.ball,p.a,p.b},machine_)))return -1;
    if(!limitIssue(p,machine_).empty()||!clearWithPad(model_,machine_,sdf_,p,t,0))return -1;
    for(double pad:{.50,.20,.08,.032,.012,.004,.001})
        if(clearWithPad(model_,machine_,sdf_,p,t,pad))return pad;
    return 0;
}
bool MotionChecks::sweep(Pose a,Pose b,const Cutter& t,const std::function<Pose(double)>& provided,double speedBound,double minimumZ,TipCheck tip)const{
    if(!limitIssue(a,machine_).empty()||!limitIssue(b,machine_).empty()||a.tip.z<minimumZ||b.tip.z<minimumZ)return false;
    auto curve=[&](double v){return provided?provided(v):interpolate(a,b,v);};
    const double breadth=t.overhang+40+t.holderRadius;
    const double da=rotationDistance(a.a,b.a),db=rotationDistance(a.b,b.b);
    // Max lever arm in the stock frame and max travel over parameter [0,1].
    const double tipSpeed=speedBound>0?speedBound:norm(b.tip-a.tip);
    const double maxTip=std::max(rotaryRadius(a.tip),rotaryRadius(b.tip))+tipSpeed;
    const double speed=tipSpeed+da*(maxTip+breadth)+db*breadth;
    const double tipAssemblySpeed=tipSpeed+da*(maxTip+t.ball+t.diameter*.5)+db*(t.ball+t.diameter*.5);
    const double mechanicalSpeed=tipSpeed+db*machine_.arm;
    auto check=[&](auto&& self,double lo,double hi,int depth)->bool{
        const auto p=curve((lo+hi)*.5);
        auto q=mechanical(p,machine_);
        if(p.tip.z<minimumZ||!limitIssue(p,machine_).empty()||!clearWithPad(model_,machine_,sdf_,p,t,0,-1,tip))return false;
        const double range=(hi-lo)*.5;
        bool limitsProven=p.tip.z-tipSpeed*range>=minimumZ;
        for(int i=0;i<3;++i)limitsProven&=inside(component(q,i),machine_.limits[i],mechanicalSpeed*range);
        if(limitsProven&&clearWithPad(model_,machine_,sdf_,p,t,speed*range,tipAssemblySpeed*range,tip))return true;
        if(depth>=(tip==TipCheck::NcRounded?17:12))return false;
        return self(self,lo,(lo+hi)*.5,depth+1)&&self(self,(lo+hi)*.5,hi,depth+1);
    };
    return check(check,0,1,0);
}

static void appendRapid(Spiral& plan,Pose p,const PathSpec& path,int pass){
    plan.moves.push_back({p,path.tool,pass,MotionKind::Rapid});
}
Spiral planSpiral(Tree& tree,const Machine& m,const Cutter& tool,const PathSpec& path,int pass,const GuideNormal& guide,const DistanceGrid* sdf,const SpiralProgress& progress){
    if(!std::isfinite(path.step)||path.step<=0||tool.overhang<tool.flute||tool.holderRadius<tool.diameter*.5)
        throw std::invalid_argument("Invalid spiral cutter or step");
    const auto box=tree.bounds();
    double x0=m.axisY?box.lo.y-m.pivot.y:box.lo.x-m.pivot.x;
    double x1=m.axisY?box.hi.y-m.pivot.y:box.hi.x-m.pivot.x;
    const double radius=std::max(1.,m.safe-50);
    const double trim=tool.holderRadius+2;
    x0=std::max(x0+trim,path.clipMin);x1=std::min(x1-trim,path.clipMax);
    if(x1<=x0)throw std::invalid_argument("Insufficient room between end fixture zones");
    const double turns=(x1-x0)/path.step;
    if(path.reverse)std::swap(x0,x1);
    const double spacing=path.pointSpacing>0?path.pointSpacing:std::max(.8,path.step);
    const double around=std::max(72.,std::ceil(2*pi*radius/spacing));
    const double count=std::max(2.,std::ceil(turns*around));
    if(!std::isfinite(count)||count>=double(std::numeric_limits<size_t>::max()/2))
        throw std::invalid_argument("Mật độ xoắn ốc vượt phạm vi đếm.");
    const auto perTurn=size_t(around),samples=size_t(count);
    constexpr size_t window=4096;
    const auto workers=std::max(1u,std::thread::hardware_concurrency());
    const size_t batchSize=std::max<size_t>(512,32*size_t(workers));
    struct Candidate{Pose p;double cost;int previous;std::optional<Pose> skim={};};
    std::vector<std::vector<Candidate>> layers;
    layers.reserve(window);
    std::optional<Pose> committed;
    MotionChecks checks(tree,m,sdf);
    Spiral result;
    auto exactContact=[&](Pose p){
        auto foot=tilt(p.tip,-p.b);
        tree.setFrame(p.a,m.axisY,m.pivot,p.b);
        const auto r=tool.ball>0?tool.ball:tool.diameter*.5;
        const auto z=tree.drop(foot.x,foot.y,r,tool.ball>0);
        if(!std::isfinite(z))return std::optional<Pose>{};
        foot.z=z;
        p.tip=tilt(foot,p.b);return std::optional<Pose>{p};
    };
    auto highOnToolAxis=[&](Pose p){
        const auto u=cutterAxis(p.b);
        p.tip=p.tip+u*((m.safe+.22-p.tip.z)/u.z);
        return p;
    };
    auto exitContact=[&](Pose contact){
        auto raised=highOnToolAxis(contact);
        if(contact.b==0)appendRapid(result,raised,path,pass);
        else {
            // With a tilted head, lift along its tool axis while feeding.
            // Vertical G00 could sweep the cone sideways through an adjacent wall.
            result.moves.push_back({raised,path.tool,pass,MotionKind::Approach});
            raised.b=0;appendRapid(result,raised,path,pass);
        }
    };
    auto enterContact=[&](Pose contact){
        auto raised=highOnToolAxis(contact),neutral=raised;
        neutral.b=0;appendRapid(result,neutral,path,pass);
        if(contact.b!=0)appendRapid(result,raised,path,pass);
        result.moves.push_back({contact,path.tool,pass,MotionKind::Approach});
    };
    auto splitAtDiscontinuity=[&](Pose from,Pose to){
        result.lifts.push_back({pass,from,"contact projection changes surface"});
        exitContact(from);enterContact(to);
    };
    auto emitContact=[&](auto&& self,Pose from,Pose to,int depth,bool atContact)->void{
        // Preserve exact sampled contacts; insert more contacts only where the
        // geometric chord drifts off the STL. This is chord subdivision,
        // not a stock allowance and not the verifier's rounding tolerance.
        if(path.allowance==0.&&atContact&&depth<=8){
            double deviation=.0008;
            std::optional<Pose> split;
            for(double t:{.25,.50,.75}){
                const auto mid=controllerPose(from,to,t,m);
                auto target=exactContact(mid);
                if(target){
                    const auto error=norm(mid.tip-target->tip);
                    if(error>deviation){deviation=error;split=target;}
                }
            }
            if(split){
                // The projected contact can jump to another sheet. Pulling a
                // neighbouring chord through that jump can hit the cone/holder.
                if(split->tip.z<path.minZ||split->tip.z>path.maxZ||
                   !clearWithPad(tree,m,sdf,*split,tool,.002,-1,TipCheck::PlannedContact)||
                   (deviation>.05&&(depth==8||std::abs(to.a-from.a)<.12))){
                    splitAtDiscontinuity(from,to);return;
                }
                if(depth==8){result.moves.push_back({to,path.tool,pass,MotionKind::Cut});return;}
                self(self,from,*split,depth+1,true);
                self(self,*split,to,depth+1,true);
                return;
            }
        }
        if(depth>0){
            const auto curve=[&](double u){return controllerPose(from,to,u,m);};
            if(!checks.sweep(from,to,tool,curve,controllerSpeed(from,to,m),path.minZ,TipCheck::PlannedContact)){
                splitAtDiscontinuity(from,to);return;
            }
        }
        result.moves.push_back({to,path.tool,pass,MotionKind::Cut});
    };
    auto bestTail=[&]{
        const auto& tail=layers.back();
        return std::min_element(tail.begin(),tail.end(),[](const Candidate& a,const Candidate& b){return a.cost<b.cost;});
    };
    auto emit=[&](bool close=true){
        if(layers.empty()){
            if(close&&committed){exitContact(*committed);committed.reset();}
            return;
        }
        std::vector<std::pair<Pose,bool>> track;
        int best=int(bestTail()-layers.back().begin());
        for(auto i=layers.rbegin();i!=layers.rend();++i){const auto& c=i->at(size_t(best));track.emplace_back(c.p,true);if(c.skim)track.emplace_back(*c.skim,false);best=c.previous;}
        std::reverse(track.begin(),track.end());
        if(!committed)enterContact(track.front().first);
        for(size_t i=1;i<track.size();++i)emitContact(emitContact,track[i-1].first,track[i].first,0,track[i-1].second&&track[i].second);
        if(close){
            exitContact(track.back().first);
            committed.reset();
            layers.clear();
        }else{
            // Keep the selected tail as the first layer of the next window.
            // Later edges must connect to this pose without adding a retract.
            committed=track.back().first;
            const auto tail=*bestTail();
            layers.clear();
            layers.push_back({Candidate{tail.p,0,-1}});
        }
    };
    auto candidatesAt=[&](Tree& query,size_t layer){
        double t=double(layer)/samples,a0=t*turns*360,x=x0+(x1-x0)*t;
        const auto normal=guide?guide(.25-a0/360,path.reverse?1-t:t):V{};
        std::vector<Candidate> candidates;
        const bool tiltHead=m.axes==5&&path.axes!=4;
        const double minB=tiltHead?path.bMin:0,maxB=tiltHead?path.bMax:0;
        for(const auto& [aOffset,yFactor]:{std::pair{0.,0.},std::pair{0.,.20},std::pair{0.,-.20},
                                        std::pair{4.,.36},std::pair{-4.,-.36},std::pair{0.,.55},std::pair{0.,-.55}}){
            double a=a0+aOffset;
            // Larger offsets are considered only when the surface or limits
            // support that actual query location, before any compensation.
            auto n=face(normal,a,m.axisY);
            const double y=radius*(yFactor+std::clamp(n.y,-1.,1.)*.12);
            double wanted=std::clamp(std::atan2(n.x,std::max(.01,n.z))*180/pi,minB,maxB);
            std::vector<double> tilts{0};
            if(tiltHead)for(double b:{wanted,std::clamp(-12.,minB,maxB),std::clamp(12.,minB,maxB)})
                if(std::find(tilts.begin(),tilts.end(),b)==tilts.end())tilts.push_back(b);
            for(double b:tilts){
                query.setFrame(a,m.axisY,m.pivot,b);
                auto foot=tilt({x,y,radius},-b);
                const double r=tool.ball>0?tool.ball:tool.diameter*.5;
                double z=query.drop(foot.x,foot.y,r,tool.ball>0);
                if(!std::isfinite(z))continue;
                if(path.allowance>0)z=std::max(z,query.drop(foot.x,foot.y,r+path.allowance,tool.ball>0)+path.allowance);
                // Drop-Cutter already returns the tip: finish uses exactly this Z.
                foot.z=z;
                Pose p{tilt(foot,b),a,b};
                if(p.tip.z<path.minZ||p.tip.z>path.maxZ||!limitIssue(p,m).empty()||!clearWithPad(query,m,sdf,p,tool,.002,-1,TipCheck::PlannedContact))continue;
                double local=.05*std::abs(yFactor)+.025*std::abs(b-wanted)+.01*std::abs(aOffset);
                // Prefer the outer surface of a region in the absence of a
                // guide and use offset/tilt as alternatives wherever needed.
                local+=.015*std::abs(p.tip.z-radius);
                candidates.push_back({p,local,-1});
            }
        }
        return candidates;
    };
    if(progress)progress(0,samples+1,perTurn);
    for(size_t base=0;base<=samples;base+=batchSize){
        const size_t size=std::min(batchSize,samples+1-base);
        std::vector<std::vector<Candidate>> batch(size);
        const size_t threads=std::min<size_t>(workers,std::max<size_t>(1,(size+31)/32));
        std::vector<std::exception_ptr> failures(threads);
        auto compute=[&](size_t id){
            try {
                auto query=tree;
                for(size_t i=id;i<size;i+=threads)batch[i]=candidatesAt(query,base+i);
            } catch(...) {failures[id]=std::current_exception();}
        };
        {
            std::vector<std::jthread> jobs;
            for(size_t id=1;id<threads;++id)jobs.emplace_back(compute,id);
            compute(0);
        }
        for(const auto& failure:failures)if(failure)std::rethrow_exception(failure);
        for(auto& candidates:batch){
            if(candidates.empty()){
                ++result.unreachable;
                if(!layers.empty()){
                    result.lifts.push_back({pass,bestTail()->p,"no reachable candidate"});
                    emit();
                }
                continue;
            }
            if(!layers.empty()){
                std::vector<Candidate> feasible;
                for(auto candidate:candidates){
                    std::vector<std::pair<double,int>> ordered;
                    auto& last=layers.back();
                    for(int i=0;i<int(last.size());++i){
                        const auto& from=last[size_t(i)];
                        if(candidate.p.a<=from.p.a)continue;
                        const double dist=norm(partPoint(candidate.p,m)-partPoint(from.p,m));
                        if(dist>std::max(1.3,path.step*1.7))continue;
                        double score=candidate.cost+from.cost+dist+std::abs(candidate.p.b-from.p.b)*.15;
                        ordered.emplace_back(score,i);
                    }
                    std::sort(ordered.begin(),ordered.end());
                    auto legal=[&](Pose from,Pose to){
                        auto curve=[&](double t){return controllerPose(from,to,t,m);};
                        return checks.sweep(from,to,tool,curve,controllerSpeed(from,to,m),path.minZ,TipCheck::PlannedContact);
                    };
                    bool found=false;
                    for(auto [score,i]:ordered){
                        if(legal(last[size_t(i)].p,candidate.p)){
                            feasible.push_back({candidate.p,score,i});found=true;break;
                        }
                    }
                    if(!found&&!ordered.empty()){
                        const auto [score,i]=ordered.front();
                        const Pose from=last[size_t(i)].p;
                        auto skim=interpolate(from,candidate.p,.5);
                        skim.tip.z=std::max(from.tip.z,candidate.p.tip.z)+.25;
                        if(skim.tip.z<m.safe&&legal(from,skim)&&legal(skim,candidate.p))
                            feasible.push_back({candidate.p,score+1.,i,skim});
                    }
                }
                if(feasible.empty()){
                    result.lifts.push_back({pass,bestTail()->p,"no swept-clear link"});emit();
                }else candidates=std::move(feasible);
            }
            layers.push_back(std::move(candidates));
            if(layers.size()>=window)emit(false);
        }
        if(progress)progress(base+size,samples+1,perTurn);
    }
    emit();
    for(auto& move:result.moves){move.feed=path.feed;move.rpm=path.rpm;}
    return result;
}
}
