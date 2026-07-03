#ifndef GA_H
#define GA_H

#include "evaluator.h"
#include "schedule.h"
#include "spanning_tree.h"
#include "etg.h"
#include "etg_prep.h"

#include <random>
#include <vector>

namespace solver {

// Evolution scheme per ETG_GA_Design_v2.md §13: population sized from the
// instance, disjoint generation fractions (beta+gamma+delta = 1), linear
// rank selection and a dynamic stop condition.
struct GaParams {
    double alpha = 1.0;          // POP = round(alpha * numTasks * numPeTypes)
    double beta  = 0.6;          // crossover fraction
    double gamma = 0.3;          // mutation fraction, (0,1)
    double delta = 0.1;          // cloning fraction, (0,1); beta+gamma+delta = 1
    double rankPressure = 1.5;   // sp in [1,2] for linear rank selection
    int noImproveLimit = 20;     // dynamic stop
    int maxGenerations = 1000;   // hard safety limit
    unsigned seed = 42;
};

struct GaResult {
    Individual best;
    int generationsRun = 0;
    bool stoppedByNoImprove = false;
};

// Number of distinct processor types (typeFlag values) present in @proc.
int countPeTypes(const etg::ETG& graph);

// Linear rank selection probabilities p(0..popSize-1), rank 0 = best.
std::vector<double> linearRankProbs(int popSize, double sp);

// Throws std::invalid_argument if the GA parameters are invalid.
void validateGaParams(const GaParams& p);

GaResult runGa(
    const etg::ETG& graph,
    const etg::PreparedData& prep,
    const SpanningTree& tree,
    const GaParams& gaParams,
    const EvalParams& evalParams);

void printSchedule(const Individual& ind, const etg::ETG& graph, std::ostream& os);

}

#endif
