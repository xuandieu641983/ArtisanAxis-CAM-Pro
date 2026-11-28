#include "motion.h"
#include <algorithm>
#include <cmath>
#include <charconv>
#include <iomanip>
#include <locale>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <exception>

namespace cam {
namespace {
double norm(V q){return std::sqrt(dot(q,q));}
double radians(double d){return std::abs(d)*std::numbers::pi/180.;}
bool tcp(const Machine& m){return m.dialect==Machine::Tcp434||m.dialect==Machine::Traori;}
V command(Pose p,const Machine& m){
    if(tcp(m))return partPoint(p,m);
    auto q=p.tip;
    if(m.dialect==Machine::Direct)q=q+headCompensation(p.b,m);
    return world(q,0,m.axisY,m.pivot);
}
Pose decode(V xyz,double a,double b,const Machine& m){
    if(tcp(m))return {face(xyz,a,m.axisY,m.pivot),a,b};
    auto q=face(xyz,0,m.axisY,m.pivot);
    if(m.dialect==Machine::Direct)q=q-headCompensation(b,m);
    return {q,a,b};
}
bool usable(std::string_view p,const Machine& m){
    if(m.axes==4)return m.dialect==Machine::Plain&&(p=="shanlong4"||p=="jd45"||p=="jd50"||p=="mach3"||p=="weihong4"||p=="syntec4"||p=="richauto");
    if(m.dialect==Machine::Tcp434)return p=="cm600"||p=="syntec5"||p=="fanuc5";
    return (m.dialect==Machine::Direct&&p=="non-rtcp")||(m.dialect==Machine::Traori&&p=="traori");
}
}
Pose controllerPose(Pose a,Pose b,double t,const Machine& m){
    auto x=command(a,m),y=command(b,m);
    auto axis=interpolate(a,b,t);
    return decode(x+(y-x)*t,axis.a,axis.b,m);
}
double controllerSpeed(Pose a,Pose b,const Machine& m){
    // A curve with a bounded second derivative differs from its secant in
    // derivative by at most that bound on a unit parameter interval.
    double speed=norm(b.tip-a.tip);
    if(tcp(m)){
        auto x=command(a,m),y=command(b,m);
        const double w=radians(a.a-b.a);
        speed+=2*w*norm(y-x)+w*w*std::max(std::hypot(face(x,0,m.axisY,m.pivot).y,(x-m.pivot).z),
                                   std::hypot(face(y,0,m.axisY,m.pivot).y,(y-m.pivot).z));
    }
    if(m.dialect==Machine::Direct){
        const double w=radians(a.b-b.b);
        speed+=headBendRadius(m)*w*w;
    }
    return speed;
}
std::string writeMotionNc(const Spiral& program,const Machine& m,std::string_view post){
    if(!usable(post,m)||program.moves.empty())throw std::invalid_argument("No post for this kinematic model");
    std::ostringstream out;
    out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(3);
    out<<"(XUAN DIEU - huynhgiaphattc@gmail.com)\n(NC REVIEW ONLY - VALIDATE CALIBRATION ON MACHINE)\n"
       <<"G21\nG90\nG94\nG54\nM5\n";
    auto row=[&](Pose p,const char* mode){
        auto q=command(p,m);
        out<<mode<<" X"<<q.x<<" Y"<<q.y<<" Z"<<q.z<<" A"<<p.a*m.aSign;
        if(m.axes==5)out<<" B"<<p.b*m.bSign;
    };
    Pose home=program.moves.front().pose;
    home.a=home.b=0;home.tip.z=std::ceil(m.safe*1000)/1000+.002;
    // At neutral rotary coordinates the TCP and workpiece frames coincide.
    // Retract vertically before the program can command any rotary or XY word.
    out<<"(START WITH HEAD NEUTRAL AND G54 CALIBRATED)\n"
       <<"G00 Z"<<(home.tip.z+m.pivot.z)<<'\n';
    row(home,"G00");out<<'\n';
    if(m.dialect==Machine::Tcp434)out<<"G43.4 H1\n";
    if(m.dialect==Machine::Traori)out<<"TRAORI\n";
    int tool=-1,pass=-1,rpm=0;
    Pose previous=home;
    for(const auto& move:program.moves){
        if(move.pass!=pass){pass=move.pass;out<<"(PASS "<<pass<<")\n";}
        if(move.tool!=tool){
            if(tool>=0)out<<"M5\nM0\n";
            const bool manual=post=="jd45"||post=="jd50"||post=="richauto"||post=="weihong4";
            tool=move.tool;
            if(manual)out<<"(MANUAL TOOL "<<tool+1<<")\n";
            out<<"T"<<tool+1<<(manual?"\nM0\n":" M6\n");
            rpm=0;
        }
        if(move.rpm!=rpm){rpm=move.rpm;out<<"S"<<rpm<<" M3\n";}
        auto p=move.pose;
        if(move.kind==MotionKind::Rapid&&p.tip.z>=m.safe)p.tip.z+=.20;
        if(move.kind==MotionKind::Rapid&&previous.tip.z>=m.safe&&p.tip.z>=m.safe){
            int steps=std::max(1,int(std::ceil(std::max(std::abs(p.a-previous.a),std::abs(p.b-previous.b))/1.5)));
            for(int i=1;i<steps;++i){
                auto safePose=interpolate(previous,p,double(i)/steps);
                safePose.tip.z=std::max(safePose.tip.z,m.safe+.20);
                row(safePose,"G00");out<<'\n';
            }
        }
        row(p,move.kind==MotionKind::Rapid?"G00":"G01");
        if(move.kind!=MotionKind::Rapid)
            out<<" F"<<(move.kind==MotionKind::Cut?move.feed:std::min(250.,move.feed));
        out<<'\n';previous=p;
    }
    if(m.dialect==Machine::Tcp434)out<<"G49\n";
    if(m.dialect==Machine::Traori)out<<"TRAFOOF\n";
    out<<"M5\nM30\n";return std::move(out).str();
}

Verified verifyMotionNc(std::string_view contents,const Machine& m,const std::vector<Cutter>& tools,MotionChecks& checks,const NcProgress& progress){
    Verified output;
    unsigned number=0;int tool=-1,pass=0;
    bool compensation=false,spindle=false,ended=false,units=false,absolute=false,feedMode=false,work=false,startupLift=false;
    std::optional<Pose> previous;V previousCommand;
    const auto bytes=contents.size();
    const auto concurrency=std::max(1u,std::thread::hardware_concurrency());
    constexpr size_t batchSize=1024;
    struct Segment {
        Pose from,to;
        V commandFrom,commandTo;
        double speed,minimumZ;
        int tool;
        unsigned line;
    };
    std::vector<Segment> queue;
    queue.reserve(batchSize);
    auto addIssue=[&](unsigned line,std::string text,SweepFailure failure=SweepFailure::None){
        if(output.errors.size()<1000)output.errors.push_back({line,std::move(text),failure});
    };
    auto problem=[&](std::string text){addIssue(number,std::move(text));};
    auto verifyQueue=[&]{
        if(queue.empty())return;
        const auto threads=std::min<size_t>(concurrency,(queue.size()+63)/64);
        std::vector<SweepFailure> failures(queue.size(),SweepFailure::None);
        std::vector<std::exception_ptr> errors(threads);
        auto compute=[&](size_t id){
            try{
                auto local=checks;
                for(size_t i=id;i<queue.size();i+=threads){
                    const auto& step=queue[i];
                    const auto actual=[&](double ratio){
                        const auto rotary=interpolate(step.from,step.to,ratio);
                        return decode(step.commandFrom+(step.commandTo-step.commandFrom)*ratio,rotary.a,rotary.b,m);
                    };
                    local.sweep(step.from,step.to,tools[size_t(step.tool)],actual,step.speed,step.minimumZ,TipCheck::NcRounded,&failures[i]);
                }
            }catch(...){errors[id]=std::current_exception();}
        };
        {
            std::vector<std::jthread> jobs;
            for(size_t i=1;i<threads;++i)jobs.emplace_back(compute,i);
            compute(0);
        }
        for(const auto& error:errors)if(error)std::rethrow_exception(error);
        for(size_t i=0;i<queue.size();++i)if(failures[i]!=SweepFailure::None)
            addIssue(queue[i].line,"Swept assembly collision or unresolved travel bound",failures[i]);
        queue.clear();
        if(progress)progress(bytes-contents.size(),bytes);
    };
    while(!contents.empty()){
        const auto end=contents.find('\n');
        auto line=contents.substr(0,end);
        contents.remove_prefix(end==std::string_view::npos?contents.size():end+1);
        ++number;
        if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);
        if(line.empty())continue;
        if(line.front()=='('&&line.back()==')'){
            if(line.starts_with("(PASS ")){
                try{size_t n=0;auto value=line.substr(6,line.size()-7);pass=std::stoi(std::string(value),&n);
                    if(n!=value.size()||pass<0||pass>1000)problem("Invalid pass number");
                }catch(...){problem("Invalid pass number");}
            }
            continue;
        }
        if(ended){problem("Block after M30");continue;}
        if(line=="G21"){units=true;continue;}
        if(line=="G90"){absolute=true;continue;}
        if(line=="G94"){feedMode=true;continue;}
        if(line=="G54"){work=true;continue;}
        if(line=="M5"){spindle=false;continue;}
        if(line=="M30"){
            if(compensation||spindle||!previous||previous->tip.z<m.safe-.0001)problem("End state is not safe");
            ended=true;continue;
        }
        if(line=="G43.4 H1"||line=="TRAORI"){
            if((line=="TRAORI")!=(m.dialect==Machine::Traori)||!tcp(m)||compensation||!previous||
               previous->tip.z<m.safe-.0001||previous->b!=0||previous->a!=0)
                problem("Unexpected TCP activation");
            compensation=true;continue;
        }
        if(line=="G49"||line=="TRAFOOF"){
            if(!compensation||((line=="TRAFOOF")!=(m.dialect==Machine::Traori))||!previous||
               previous->tip.z<m.safe-.0001||previous->b!=0)problem("Unexpected TCP cancellation");
            compensation=false;continue;
        }
        if(line=="M0"){
            if(spindle||!previous||previous->tip.z<m.safe-.0001||previous->b!=0)problem("Unsafe tool stop");
            continue;
        }
        if(line.front()=='S'&&line.ends_with(" M3")){
            const auto number=line.substr(1,line.size()-4);
            int rpm=0;
            const auto parsed=std::from_chars(number.data(),number.data()+number.size(),rpm);
            if(parsed.ec!=std::errc{}||parsed.ptr!=number.data()+number.size()||rpm<=0||rpm>60000)
                problem("Invalid spindle speed");
            else spindle=true;
            continue;
        }
        if(line.front()=='T'){
            auto i=line.find(' ');
            try{
                auto s=line.substr(1,i==std::string::npos?line.size()-1:i-1);size_t n=0;
                int t=std::stoi(std::string(s),&n)-1;
                if(n!=s.size()||t<0||t>=int(tools.size())||(i!=std::string::npos&&line.substr(i)!=" M6"))
                    problem("Unsupported tool block");
                else if(spindle||!previous||previous->tip.z<m.safe-.0001||previous->b!=0)problem("Tool change below safe plane");
                else tool=t;
            }catch(...){problem("Invalid tool number");}
            continue;
        }
        const auto opcode=line.substr(0,line.find(' '));
        const bool rapid=opcode=="G00";
        if(!rapid&&opcode!="G01"){problem("Unsupported NC block");continue;}
        auto fields=line.substr(opcode.size());
        std::array<std::optional<double>,6> words;
        bool valid=true;
        while(!fields.empty()){
            if(fields.front()==' '||fields.front()=='\t'){fields.remove_prefix(1);continue;}
            const auto word=fields.substr(0,fields.find_first_of(" \t"));
            fields.remove_prefix(word.size());
            const auto index=std::string_view("XYZABF").find(word.front());
            if(index==std::string_view::npos||words[index]||word.size()==1){valid=false;break;}
            double value;
            const auto parsed=std::from_chars(word.data()+1,word.data()+word.size(),value);
            if(parsed.ec!=std::errc{}||parsed.ptr!=word.data()+word.size()||!std::isfinite(value)){valid=false;break;}
            words[index]=value;
        }
        if(valid&&rapid&&!previous&&words[2]&&!words[0]&&!words[1]&&!words[3]&&!words[4]&&!words[5]){
            const auto localZ=*words[2]-m.pivot.z;
            const auto machineZ=*words[2]+m.machineOffset.z;
            const auto limits=m.limits[2];
            if(startupLift||!units||!absolute||!feedMode||!work||localZ<m.safe-.0001||
               (limits&&(machineZ<(*limits)[0]||machineZ>(*limits)[1])))
                problem("Invalid startup Z retract");
            startupLift=true;
            continue;
        }
        if(!valid||!words[0]||!words[1]||!words[2]||!words[3]||(words[4].has_value()!=(m.axes==5))||
           (rapid&&words[5])||(!rapid&&(!words[5]||*words[5]<=0))){
            problem("Invalid or unexpected axis/feed word");continue;
        }
        V xyz{*words[0],*words[1],*words[2]};
        auto pose=decode(xyz,*words[3]/m.aSign,words[4].value_or(0)/m.bSign,m);
        ++output.blocks;
        if(!units||!absolute||!feedMode||!work)problem("Missing program modes");
        if(auto issue=limitIssue(pose,m);!issue.empty())problem(issue);
        if(!previous){
            if(!rapid||pose.a!=0||pose.b!=0||pose.tip.z<m.safe-.0001)problem("First pose is not neutral and safe");
        }else{
            if(tcp(m)&&!compensation)problem("Motion while TCP is disabled");
            if(tool<0)problem("Motion with no selected tool");
            if(!rapid&&!spindle)problem("Feed with spindle off");
            const bool rot=pose.a!=previous->a||pose.b!=previous->b;
            const bool lateral=std::hypot(pose.tip.x-previous->tip.x,pose.tip.y-previous->tip.y)>.0015;
            if(rapid&&(rot||lateral)&&(pose.tip.z<m.safe-.0001||previous->tip.z<m.safe-.0001))
                problem("Rapid rotary or lateral movement below safe plane");
            const double speed=controllerSpeed(*previous,pose,m);
            auto actual=[&](double k){
                auto rotPose=interpolate(*previous,pose,k);
                return decode(previousCommand+(xyz-previousCommand)*k,rotPose.a,rotPose.b,m);
            };
            if(rapid&&rot&&(tcp(m)||m.dialect==Machine::Direct)&&
                actual(.5).tip.z<m.safe-.0001)problem("Interpolated rapid drops below safe plane");
            if(tool>=0){
                queue.push_back({*previous,pose,previousCommand,xyz,speed,
                    rapid&&rot?m.safe-.0001:-std::numeric_limits<double>::infinity(),tool,number});
                if(queue.size()==batchSize)verifyQueue();
            }
        }
        auto kind=rapid?MotionKind::Rapid:MotionKind::Cut;
        if(!rapid&&previous){
            const bool wasClear=previous->tip.z>=m.safe-.0001;
            const bool isClear=pose.tip.z>=m.safe-.0001;
            if(!wasClear&&isClear)kind=MotionKind::Retract;
            else if(wasClear)kind=MotionKind::Approach;
        }
        output.motions.push_back({pose,tool,pass,kind,number,words[5].value_or(3000)});
        previous=pose;previousCommand=xyz;
    }
    verifyQueue();
    if(!ended)problem("Missing M30");
    std::stable_sort(output.errors.begin(),output.errors.end(),[](const Diagnostic& a,const Diagnostic& b){return a.line<b.line;});
    return output;
}
}
