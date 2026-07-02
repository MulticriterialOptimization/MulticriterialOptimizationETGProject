#include "genes.h"

#include <array>
#include <cstddef>

namespace solver {

int peGeneCount() {
    return static_cast<int>(PeGene::COUNT);
}

int clsGeneCount() {
    return static_cast<int>(ClsGene::COUNT);
}

namespace {

constexpr std::array<double, 8> kPeWeights = {
    12.0,
    12.0,
    12.0,
    12.0,
    12.0,
    10.0,
    10.0,
    20.0,
};
static_assert(kPeWeights.size() == static_cast<std::size_t>(PeGene::COUNT),
              "kPeWeights must have one entry per PeGene");

constexpr std::array<double, 7> kClsWeights = {
    12.0,
    12.0,
    18.0,
    18.0,
    10.0,
    10.0,
    20.0,
};
static_assert(kClsWeights.size() == static_cast<std::size_t>(ClsGene::COUNT),
              "kClsWeights must have one entry per ClsGene");

}

PeGene randomPeGene(std::mt19937& rng) {
    static std::discrete_distribution<int> dist(kPeWeights.begin(), kPeWeights.end());
    return static_cast<PeGene>(dist(rng));
}

ClsGene randomClsGene(std::mt19937& rng) {
    static std::discrete_distribution<int> dist(kClsWeights.begin(), kClsWeights.end());
    return static_cast<ClsGene>(dist(rng));
}

}
