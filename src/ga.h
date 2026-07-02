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

struct GaParams {
    int populationSize = 50;
    int generations = 100;
    int eliteCount = 2;
    int tournamentSize = 3;
    double crossoverRate = 0.7;
    double mutationRate = 0.1;
    unsigned seed = 42;
};

struct GaResult {
    Individual best;
    int generationsRun = 0;
};

GaResult runGa(
    const etg::ETG& graph,
    const etg::PreparedData& prep,
    const SpanningTree& tree,
    const GaParams& gaParams,
    const EvalParams& evalParams);

void printSchedule(const Individual& ind, const etg::ETG& graph, std::ostream& os);

}

#endif
