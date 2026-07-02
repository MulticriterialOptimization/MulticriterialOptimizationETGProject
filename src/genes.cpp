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

// Effective gene weights per ETG_GA_Design_v2.md §4.3. The two-level slide
// distribution (60% "Allocated" split internally, plus the global genes) is
// flattened into per-gene weights: e.g. AllocCheapest = 0.60 * 20% = 12.
// Order MUST match the enum declarations in genes.h.
namespace {

constexpr std::array<double, 8> kPeWeights = {
    12.0, // AllocCheapest   (60% * 20%)
    12.0, // AllocFastest    (60% * 20%)
    12.0, // AllocLFU        (60% * 20%)
    12.0, // AllocIdle       (60% * 20%)
    12.0, // AllocSamePred   (60% * 20%)
    10.0, // Cheapest
    10.0, // Fastest
    20.0, // MinTS
};
static_assert(kPeWeights.size() == static_cast<std::size_t>(PeGene::COUNT),
              "kPeWeights must have one entry per PeGene");

constexpr std::array<double, 7> kClsWeights = {
    12.0, // ClsAllocCheapest (60% * 20%)
    12.0, // ClsAllocFastest  (60% * 20%)
    18.0, // ClsAllocLFU      (60% * 30%)
    18.0, // ClsAllocIdle     (60% * 30%)
    10.0, // ClsCheapest
    10.0, // ClsHighestBw
    20.0, // ClsLFU
};
static_assert(kClsWeights.size() == static_cast<std::size_t>(ClsGene::COUNT),
              "kClsWeights must have one entry per ClsGene");

} // namespace

PeGene randomPeGene(std::mt19937& rng) {
    static std::discrete_distribution<int> dist(kPeWeights.begin(), kPeWeights.end());
    return static_cast<PeGene>(dist(rng));
}

ClsGene randomClsGene(std::mt19937& rng) {
    static std::discrete_distribution<int> dist(kClsWeights.begin(), kClsWeights.end());
    return static_cast<ClsGene>(dist(rng));
}

}
