#include "genes.h"

namespace solver {

int peGeneCount() {
    return static_cast<int>(PeGene::COUNT);
}

int clsGeneCount() {
    return static_cast<int>(ClsGene::COUNT);
}

PeGene randomPeGene(std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(0, peGeneCount() - 1);
    return static_cast<PeGene>(dist(rng));
}

ClsGene randomClsGene(std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(0, clsGeneCount() - 1);
    return static_cast<ClsGene>(dist(rng));
}

}
