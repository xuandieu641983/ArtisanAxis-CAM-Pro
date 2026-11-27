#include "nc.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cam {
std::string writeNc(const Plan& plan,std::string_view post,bool axisY,V pivot,int sign){
    const bool manual=post=="richauto"||post=="jd45"||post=="jd50"||post=="weihong4";
    if(!manual&&post!="mach3"&&post!="shanlong4"&&post!="syntec4")
        throw std::invalid_argument("Post 4X chưa có cấu hình xuất NC.");
    if(plan.cuts.empty()||!std::isfinite(plan.safe)||plan.safe<50||(sign!=1&&sign!=-1))
        throw std::invalid_argument("Đường dao, chiều quay hoặc mức nhấc không hợp lệ.");
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out<<std::fixed<<std::setprecision(3);
    const double safe=std::ceil(std::max(plan.safe,plan.safe+pivot.z)*1000)/1000;
    out<<"(XUAN DIEU - huynhgiaphattc@gmail.com)\n(NC REVIEW ONLY - VERIFY MACHINE AND FIXTURES)\n"
       <<"G21 G90 G94\nG54\nM5\nG00 Z"<<safe<<"\n";
    int tool=-1;
    double angle=std::numeric_limits<double>::quiet_NaN();
    for(size_t i=0;i<plan.cuts.size();++i){
        const auto& cut=plan.cuts[i];
        const auto p=world(cut.point,0,axisY,pivot);
        if(!(p.z<safe)||!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(cut.angle))
            throw std::invalid_argument("Toa do NC vuot muc nhac.");
        const bool linked=cut.link&&i&&cut.tool==plan.cuts[i-1].tool&&cut.angle==plan.cuts[i-1].angle;
        if(tool!=cut.tool){
            if(tool>=0)out<<"M5\nG00 Z"<<safe<<"\n";
            tool=cut.tool;
            if(manual)out<<"(MANUAL TOOL "<<tool+1<<")\nM0\n";
            else {if(i)out<<"M0\n";out<<"T"<<tool+1<<" M6\n";}
            out<<"S16000 M3\n";
        }
        if(cut.angle!=angle){
            out<<"G00 Z"<<safe<<"\nG00 A"<<sign*cut.angle<<"\n";angle=cut.angle;
        }
        if(linked)out<<"G01 X"<<p.x<<" Y"<<p.y<<" Z"<<std::ceil(p.z*1000)/1000<<" F600\n";
        else out<<"G00 Z"<<safe<<"\nG00 X"<<p.x<<" Y"<<p.y<<"\nG01 Z"<<std::ceil(p.z*1000)/1000<<" F300\n";
    }
    out<<"G00 Z"<<safe<<"\nM5\nM30\n";
    return out.str();
}
}
