#ifndef GENES_H
#define GENES_H

#include <random>

namespace solver {

enum class PeGene {
    AllocCheapest,
    AllocFastest,
    AllocLFU,
    AllocIdle,
    AllocSamePred,
    Cheapest,
    Fastest,
    MinTS,
    COUNT
};

enum class ClsGene {
    ClsAllocCheapest,
    ClsAllocFastest,
    ClsAllocLFU,
    ClsAllocIdle,
    ClsCheapest,
    ClsHighestBw,
    ClsLFU,
    COUNT
};

int peGeneCount();
int clsGeneCount();

PeGene randomPeGene(std::mt19937& rng);
ClsGene randomClsGene(std::mt19937& rng);

}

#endif
