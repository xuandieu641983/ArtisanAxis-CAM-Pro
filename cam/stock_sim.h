#pragma once
#include "motion.h"
namespace cam {
struct StockStage {
    int group=0;
    std::vector<V> shell,excessMap,gougeMap;
    size_t remaining=0,excess=0,gouge=0;
};
struct StockSim {
    double pitch=0;
    std::vector<StockStage> stages;
    size_t stockWarnings=0;
    std::vector<unsigned> warningMoves;
};
StockSim simulateStock(const StockSpec&,const Spiral&,const Machine&,const std::vector<Cutter>&,
                       const std::vector<int>& passGroups,const Tree& model);
}
