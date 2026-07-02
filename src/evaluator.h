#ifndef EVALUATOR_H
#define EVALUATOR_H

#include "schedule.h"
#include "spanning_tree.h"
#include "etg.h"
#include "etg_prep.h"

namespace solver {

struct EvalParams {
    int tmax = 0;
    double lambda = 1000.0;
    double penalty = 1e12;
};

void evaluateIndividual(
    Individual& ind,
    const etg::ETG& graph,
    const etg::PreparedData& prep,
    const SpanningTree& tree,
    const EvalParams& params);

}

#endif
