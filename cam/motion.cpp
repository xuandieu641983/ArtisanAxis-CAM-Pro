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
V neutralLever(const Machine& m){return m.headPivotFromTip.value_or(V{0,0,m.arm});}
bool inside(double v,const std::optional<std::array<double,2>>& bounds,double pad=0){
    return std::isfinite(v)&&(!bounds||(v-pad>=(*bounds)[0]&&v+pad<=(*bounds)[1]));
}
double rotationDistance(double a,double b){return std::abs(a-b)*pi/180.;}
}
V partPoint(Pose p,const Machine& m){return world(p.tip,p.a,m.axisY,m.pivot);}
V partToolAxis(Pose p,const Machine& m){return world(cutterAxis(p.b),p.a,m.axisY);}
V slerpToolAxis(V from,V to,double t){
    const double angle=std::acos(std::clamp(dot(from,to),-1.,1.));
    if(angle<1e-7)return unit(from*(1-t)+to*t);
    // Tool directions accepted by this planner are never antiparallel,
    // but keep the interpolation defined for all unit-vector callers.
    auto direction=unit(to-from*std::cos(angle));
    if(dot(direction,direction)==0)
        direction=unit(cross(from,std::abs(from.x)<.8?V{1,0,0}:V{0,1,0}));
    return unit(from*std::cos(angle*t)+direction*std::sin(angle*t));
}
V headCompensation(double b,const Machine& m){
    const auto lever=neutralLever(m);
    return tilt(lever,b)-lever;
}
double headBendRadius(const Machine& m){
    const auto lever=neutralLever(m);
    return std::hypot(lever.x,lever.z);
}
V mechanical(Pose p,const Machine& m){return world(p.tip+headCompensation(p.b,m),0,m.axisY,m.pivot)+m.machineOffset;}
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
        if((!sdf||sdf->lowerBound(center)<=r)&&
           (tree.distance(center)<=r||tree.insideClosedMesh(center)))return false;
    }else if(tip!=TipCheck::PlannedContact&&t.ball<=0&&
             tree.height(q.x,q.y,t.diameter*.5+tipPad)>=q.z-tipPad+tolerance)return false;
    constexpr int bands=16;
    std::array<Span,bands+3> envelope;
    size_t spanCount=0;
    auto add=[&](double low,double high,double radius){
        if(high>low)envelope[spanCount++]={low-pad,high+pad,radius+pad};
    };
    if(t.ball>0&&t.halfAngle>0){
        double join=t.ball*std::cos(t.halfAngle*pi/180.);
        double rMax=t.diameter*.5;
        for(int k=0;k<bands;++k){
            double a=join+(rMax-join)*k/bands,b=join+(rMax-join)*(k+1)/bands;
            auto low=t.profileHeight(a);
            add(low,t.flute,b);
        }
        add(t.profileHeight(rMax),t.flute,rMax);
    }else if(t.ball>0)add(t.ball,t.flute,t.diameter*.5);
    add(t.flute,t.overhang,t.diameter*.5);
    add(t.overhang,t.overhang+40,t.holderRadius);
    return tree.spansClear(q,std::span(envelope).first(spanCount));
}
double MotionChecks::clearance(Pose p,const Cutter& t)const{
    if(t.ball>0&&model_.insideClosedMesh(partPoint({p.tip+cutterAxis(p.b)*t.ball,p.a,p.b},machine_)))return -1;
    if(!limitIssue(p,machine_).empty()||!clearWithPad(model_,machine_,sdf_,p,t,0))return -1;
    for(double pad:{.50,.20,.08,.032,.012,.004,.001})
        if(clearWithPad(model_,machine_,sdf_,p,t,pad))return pad;
    return 0;
}
bool MotionChecks::sweep(Pose a,Pose b,const Cutter& t,const std::function<Pose(double)>& provided,double speedBound,double minimumZ,TipCheck tip,SweepFailure* failure)const{
    if(failure)*failure=SweepFailure::None;
    auto fail=[&](SweepFailure reason){if(failure)*failure=reason;return false;};
    if(!limitIssue(a,machine_).empty()||!limitIssue(b,machine_).empty()||a.tip.z<minimumZ||b.tip.z<minimumZ)
        return fail(SweepFailure::Travel);
    if(tip!=TipCheck::PlannedContact&&
       (!clearWithPad(model_,machine_,sdf_,a,t,0,-1,tip)||!clearWithPad(model_,machine_,sdf_,b,t,0,-1,tip)))
        return fail(SweepFailure::Assembly);
    auto curve=[&](double v){return provided?provided(v):interpolate(a,b,v);};
    const double breadth=t.overhang+40+t.holderRadius;
    const double da=rotationDistance(a.a,b.a),db=rotationDistance(a.b,b.b);
    // Max lever arm in the stock frame and max travel over parameter [0,1].
    const double tipSpeed=speedBound>0?speedBound:norm(b.tip-a.tip);
    const double maxTip=std::max(rotaryRadius(a.tip),rotaryRadius(b.tip))+tipSpeed;
    const double speed=tipSpeed+da*(maxTip+breadth)+db*breadth;
    const double tipAssemblySpeed=tipSpeed+da*(maxTip+t.ball+t.diameter*.5)+db*(t.ball+t.diameter*.5);
    const double mechanicalSpeed=tipSpeed+db*headBendRadius(machine_);
    auto check=[&](auto&& self,double lo,double hi,int depth)->bool{
        const auto p=curve((lo+hi)*.5);
        auto q=mechanical(p,machine_);
        if(p.tip.z<minimumZ||!limitIssue(p,machine_).empty())return fail(SweepFailure::Travel);
        if(!clearWithPad(model_,machine_,sdf_,p,t,0,-1,tip))return fail(SweepFailure::Assembly);
        const double range=(hi-lo)*.5;
        bool limitsProven=p.tip.z-tipSpeed*range>=minimumZ;
        for(int i=0;i<3;++i)limitsProven&=inside(component(q,i),machine_.limits[i],mechanicalSpeed*range);
        if(limitsProven&&clearWithPad(model_,machine_,sdf_,p,t,speed*range,tipAssemblySpeed*range,tip))return true;
        if(depth>=(tip==TipCheck::NcRounded?17:12))return fail(SweepFailure::Unresolved);
        return self(self,lo,(lo+hi)*.5,depth+1)&&self(self,(lo+hi)*.5,hi,depth+1);
    };
    return check(check,0,1,0);
}

static void appendRapid(Spiral& plan,Pose p,const PathSpec& path,int pass){
    plan.moves.push_back({p,path.tool,pass,MotionKind::Rapid});
}
Spiral planSpiral(Tree& tree,const Machine& m,const Cutter& tool,const PathSpec& path,int pass,const GuideNormal& guide,const DistanceGrid* sdf,const SpiralProgress& progress,const DriveGuide& surface){
    if(!std::isfinite(path.step)||path.step<=0||tool.overhang<tool.flute||tool.holderRadius<tool.diameter*.5)
        throw std::invalid_argument("Invalid spiral cutter or step");
    if(!std::isfinite(path.lead)||!std::isfinite(path.lean)||std::abs(path.lead)>60||std::abs(path.lean)>60)
        throw std::invalid_argument("Lead/Lean angle outside [-60°, 60°]");
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
    const bool tiltHead=m.axes==5&&path.axes!=4;
    const bool biasAxis=tiltHead&&(path.lead!=0||path.lean!=0);
    double minB=tiltHead?path.bMin:0,maxB=tiltHead?path.bMax:0;
    if(tiltHead&&m.limits[4]){
        const auto limits=*m.limits[4];
        const double lo=limits[0]/m.bSign,hi=limits[1]/m.bSign;
        minB=std::max(minB,std::min(lo,hi));maxB=std::min(maxB,std::max(lo,hi));
    }
    if(!(minB<=0&&maxB>=0&&minB>=-60&&maxB<=60))
        throw std::invalid_argument("B must reach zero for the safety retract");
    constexpr size_t window=4096;
    const auto workers=std::max(1u,std::thread::hardware_concurrency());
    const size_t batchSize=std::max<size_t>(512,32*size_t(workers));
    struct Candidate{Pose p;double cost;int previous;std::optional<Pose> skim={};};
    std::vector<std::vector<Candidate>> layers;
    layers.reserve(window);
    std::optional<Pose> committed;
    MotionChecks checks(tree,m,sdf);
    Spiral result;
    auto project=[&](Tree& query,Pose p){
        auto foot=tilt(p.tip,-p.b);
        query.setFrame(p.a,m.axisY,m.pivot,p.b);
        const auto r=tool.ball>0?tool.ball:tool.diameter*.5;
        double z=query.drop(foot.x,foot.y,r,tool.ball>0);
        if(!std::isfinite(z))return std::optional<Pose>{};
        if(path.allowance>0)z=std::max(z,query.drop(foot.x,foot.y,r+path.allowance,tool.ball>0)+path.allowance);
        foot.z=z;
        p.tip=tilt(foot,p.b);return std::optional<Pose>{p};
    };
    auto legalLink=[&](Pose from,Pose to,TipCheck tip){
        const auto curve=[&](double t){return controllerPose(from,to,t,m);};
        return checks.sweep(from,to,tool,curve,controllerSpeed(from,to,m),path.minZ,tip);
    };
    auto highOnToolAxis=[&](Pose p){
        const auto u=cutterAxis(p.b);
        p.tip=p.tip+u*((m.safe+.22-p.tip.z)/u.z);
        return p;
    };
    auto exitContact=[&](Pose contact){
        auto raised=highOnToolAxis(contact);
        if(m.axes==4)appendRapid(result,raised,path,pass);
        else{
            // Leave the stock at feed along the current tool axis. Rotary and
            // lateral rapid moves start only from the safe plane.
            result.moves.push_back({raised,path.tool,pass,MotionKind::Retract});
        }
        if(contact.b!=0){
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
                auto target=project(tree,mid);
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
                if(depth==8){splitAtDiscontinuity(from,to);return;}
                self(self,from,*split,depth+1,true);
                self(self,*split,to,depth+1,true);
                return;
            }
        }
        if(depth>0){
            if(!legalLink(from,to,TipCheck::PlannedContact)){
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
        if(tiltHead&&track.size()>2){
            // Relax the selected axis, then re-solve exact STL contact at the
            // achievable head angle. Keep the first/last samples unchanged:
            // a streamed window must join the next one at the committed pose.
            for(size_t i=1;i+1<track.size();++i){
                if(!track[i-1].second||!track[i].second||!track[i+1].second)continue;
                const Pose from=track[i-1].first,current=track[i].first,to=track[i+1].first;
                if(std::abs(current.b-from.b)+std::abs(to.b-current.b)<.05)continue;
                const auto target=slerpToolAxis(partToolAxis(from,m),partToolAxis(to,m),(current.a-from.a)/(to.a-from.a));
                const auto axis=face(slerpToolAxis(partToolAxis(current,m),target,.5),current.a,m.axisY);
                const double b=std::clamp(std::atan2(axis.x,axis.z)*180/pi,minB,maxB);
                if(std::abs(b-current.b)<.025)continue;
                const auto projected=project(tree,{current.tip+(cutterAxis(current.b)-cutterAxis(b))*tool.ball,current.a,b});
                if(!projected||projected->tip.z<path.minZ||projected->tip.z>path.maxZ||
                   norm(projected->tip-current.tip)>std::max(.5,path.step)||
                   !clearWithPad(tree,m,sdf,*projected,tool,.002,-1,TipCheck::PlannedContact))continue;
                if(legalLink(from,*projected,TipCheck::NcRounded)&&legalLink(*projected,to,TipCheck::NcRounded))
                    track[i].first=*projected;
            }
        }
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
    const double risePerRadian=(x1-x0)/(turns*2*pi);
    const double lead=std::tan(path.lead*pi/180),lean=std::tan(path.lean*pi/180);
    auto targetAxis=[&](V n,V anchor){
        if(dot(n,n)==0)n={0,0,1};
        if(!biasAxis)return n;
        // Relative motion on the part is axial travel plus motion around A.
        // Project that direction to the plane tangent to the drive surface.
        const V tangent{risePerRadian,anchor.z,-anchor.y};
        V forward=unit(tangent-n*dot(tangent,n));
        if(dot(forward,forward)==0)forward=unit({n.z,0,-n.x});
        if(dot(forward,forward)==0)forward={1,0,0};
        return unit(n+forward*lead+cross(n,forward)*lean);
    };
    auto candidatesAt=[&](Tree& query,size_t layer){
        const double t=double(layer)/samples,a0=t*turns*360,x=std::lerp(x0,x1,t);
        const double v=path.reverse?1-t:t;
        const auto normal=guide?guide(.25-a0/360,v):V{};
        double preferredA=0;
        if(biasAxis){
            const auto p=surface?surface(.25-a0/360,x):DriveSample{world({x,0,radius},a0,m.axisY,m.pivot),normal};
            const auto desired=targetAxis(face(p.normal,a0,m.axisY),face(p.point,a0,m.axisY,m.pivot));
            // A table rotation changes the lean relative to the part. The
            // B head alone cannot supply a second independent lateral tilt.
            preferredA=std::clamp(std::atan2(desired.y,desired.z)*180/pi,-25.,25.);
        }
        std::vector<Candidate> candidates;
        auto propose=[&](double aOffset,double yFactor,double leanCompensation){
            const double a=a0+aOffset;
            V anchor{x,0,radius};
            V n=face(normal,a,m.axisY);
            if(surface){
                // The drive surface supplies the projection ray seed, not
                // the finished height or cutter compensation.
                const auto seed=surface(.25-(a-leanCompensation)/360-std::atan(yFactor)/(2*pi),x);
                anchor=face(seed.point,a,m.axisY,m.pivot);
                n=face(seed.normal,a,m.axisY);
            }else anchor.y=radius*(yFactor+std::clamp(n.y,-1.,1.)*.12);
            const auto axis=targetAxis(n,anchor);
            const double wanted=std::clamp(std::atan2(axis.x,std::max(.01,axis.z))*180/pi,minB,maxB);
            const double tryTilts[]={wanted,0.,std::clamp(-12.,minB,maxB),std::clamp(12.,minB,maxB)};
            for(size_t option=0;option<(tiltHead?std::size(tryTilts):1);++option){
                const double b=tryTilts[option];
                if(std::find(tryTilts,tryTilts+option,b)!=tryTilts+option)continue;
                const auto hit=project(query,{anchor,a,b});
                if(!hit||hit->tip.z<path.minZ||hit->tip.z>path.maxZ||!limitIssue(*hit,m).empty()||
                   !clearWithPad(query,m,sdf,*hit,tool,.002,-1,TipCheck::PlannedContact))continue;
                double cost=.05*std::abs(yFactor)+.025*std::abs(b-wanted)+.01*std::abs(aOffset-preferredA);
                cost+=.015*std::abs(hit->tip.z-radius);
                if(biasAxis)cost+=2*(1-dot(cutterAxis(b),axis));
                candidates.push_back({*hit,cost,-1});
                // Off-centre rays on a fitted envelope are separate fallback
                // branches. Keep multiple B options on the central ray, but
                // stop each lateral branch at its first clear orientation.
                if(surface&&yFactor!=0)break;
            }
        };
        for(const auto& [aOffset,yFactor]:{std::pair{0.,0.},std::pair{0.,.20},std::pair{0.,-.20},
                                        std::pair{4.,.36},std::pair{-4.,-.36},std::pair{0.,.55},std::pair{0.,-.55}})
            propose(aOffset+preferredA,yFactor,preferredA);
        if(std::abs(preferredA)>.05)propose(0,0,0);
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
                        auto legal=[&](Pose from,Pose to){return legalLink(from,to,TipCheck::PlannedContact);};
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
                        if(skim.tip.z<m.safe&&legalLink(from,skim,TipCheck::NcRounded)&&legalLink(skim,candidate.p,TipCheck::NcRounded))
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
