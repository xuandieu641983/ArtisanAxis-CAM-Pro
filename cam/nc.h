#pragma once
#include "geometry.h"
#include <string>
#include <string_view>
namespace cam {
std::string writeNc(const Plan&,std::string_view post,bool axisY,V pivot,int sign);
}
