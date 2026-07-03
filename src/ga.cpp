#include "ga.h"

#include "genes.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace solver {

namespace {

Individual makeRandomIndividual(int numTasks, std::mt19937& rng) {
    Individual ind;
    ind.peGenes.resize(numTasks);
    ind.clsGenes.resize(numTasks);
    for (int t = 0; t < numTasks; ++t) {
        ind.peGenes[t] = randomPeGene(rng);
        ind.clsGenes[t] = randomClsGene(rng);
    }
    return ind;
}

bool inSubtree(const SpanningTree& tree, int node, int root) {
    for (int cur = node; cur >= 0; cur = tree.parent[cur]) {
        if (cur == root)
            return true;
    }
    return false;
}

// Subtree crossover (§9.3): child = copy of A, genes of the whole subtree
// rooted at a random node are taken from B. The tree shape is shared by all
// individuals, so the operation is always structurally safe.
Individual subtreeCrossover(const Individual& a, const Individual& b,
                            const SpanningTree& tree, std::mt19937& rng)
{
    Individual child;
    child.peGenes = a.peGenes;
    child.clsGenes = a.clsGenes;

    int n = static_cast<int>(child.peGenes.size());
    if (n == 0)
        return child;

    std::uniform_int_distribution<int> nodeDist(0, n - 1);
    int root = nodeDist(rng);

    for (int t = 0; t < n; ++t) {
        if (inSubtree(tree, t, root)) {
            child.peGenes[t] = b.peGenes[t];
            child.clsGenes[t] = b.clsGenes[t];
        }
    }
    return child;
}

// The mutant count is fixed by gamma, so the operator always replaces a gene
// in one random node (never a no-op).
void mutateForce(Individual& ind, std::mt19937& rng) {
    if (ind.peGenes.empty())
        return;

    std::uniform_int_distribution<int> taskDist(0, static_cast<int>(ind.peGenes.size()) - 1);
    std::uniform_int_distribution<int> coin(0, 1);
    int t = taskDist(rng);

    if (coin(rng) == 0) {
        PeGene g = ind.peGenes[t];
        for (int i = 0; i < 16 && g == ind.peGenes[t]; ++i)
            g = randomPeGene(rng);
        ind.peGenes[t] = g;
    } else {
        ClsGene g = ind.clsGenes[t];
        for (int i = 0; i < 16 && g == ind.clsGenes[t]; ++i)
            g = randomClsGene(rng);
        ind.clsGenes[t] = g;
    }
}

void evaluateAll(std::vector<Individual>& pop, const etg::ETG& graph,
                 const etg::PreparedData& prep, const SpanningTree& tree,
                 const EvalParams& evalParams)
{
    for (Individual& ind : pop)
        evaluateIndividual(ind, graph, prep, tree, evalParams);
}

bool fitnessLess(const Individual& a, const Individual& b) {
    return a.fitness < b.fitness;
}

} // namespace

int countPeTypes(const etg::ETG& graph) {
    bool universal = false;
    bool specialized = false;
    for (const etg::Processor& p : graph.procs) {
        if (p.isUniversal())
            universal = true;
        else
            specialized = true;
    }
    int types = (universal ? 1 : 0) + (specialized ? 1 : 0);
    return types > 0 ? types : 1;
}

std::vector<double> linearRankProbs(int popSize, double sp) {
    std::vector<double> probs(popSize, 1.0);
    if (popSize <= 1)
        return probs;
    for (int i = 0; i < popSize; ++i) {
        probs[i] = (sp - (2.0 * sp - 2.0) * static_cast<double>(i)
                    / static_cast<double>(popSize - 1))
                   / static_cast<double>(popSize);
    }
    return probs;
}

void validateGaParams(const GaParams& p) {
    if (p.alpha <= 0.0)
        throw std::invalid_argument("GaParams: alpha must be > 0");
    if (p.beta <= 0.0 || p.beta >= 1.0)
        throw std::invalid_argument("GaParams: beta must be in (0,1)");
    if (p.gamma <= 0.0 || p.gamma >= 1.0)
        throw std::invalid_argument("GaParams: gamma must be in (0,1)");
    if (p.delta <= 0.0 || p.delta >= 1.0)
        throw std::invalid_argument("GaParams: delta must be in (0,1)");
    if (std::abs(p.beta + p.gamma + p.delta - 1.0) > 1e-9)
        throw std::invalid_argument("GaParams: beta + gamma + delta must equal 1");
    if (p.rankPressure < 1.0 || p.rankPressure > 2.0)
        throw std::invalid_argument("GaParams: rank pressure must be in [1,2]");
    if (p.noImproveLimit <= 0)
        throw std::invalid_argument("GaParams: no-improve limit must be > 0");
    if (p.maxGenerations <= 0)
        throw std::invalid_argument("GaParams: max generations must be > 0");
}

GaResult runGa(
    const etg::ETG& graph,
    const etg::PreparedData& prep,
    const SpanningTree& tree,
    const GaParams& gaParams,
    const EvalParams& evalParams)
{
    validateGaParams(gaParams);

    std::mt19937 rng(gaParams.seed);

    const int tau = countPeTypes(graph);
    const int popSize = std::max(2, static_cast<int>(
        std::lround(gaParams.alpha * graph.numTasks * tau)));

    std::vector<Individual> pop;
    pop.reserve(popSize);
    for (int i = 0; i < popSize; ++i)
        pop.push_back(makeRandomIndividual(graph.numTasks, rng));

    evaluateAll(pop, graph, prep, tree, evalParams);
    std::sort(pop.begin(), pop.end(), fitnessLess);

    GaResult result;
    result.best = pop[0];
    double bestFit = result.best.fitness;
    int noImprove = 0;
    const double eps = 1e-9;

    // Disjoint generation fractions (§13.1); nClone absorbs rounding so the
    // sum is always exactly popSize, and at least one clone (the elite) stays.
    int nCross = static_cast<int>(std::lround(gaParams.beta * popSize));
    int nMut = static_cast<int>(std::lround(gaParams.gamma * popSize));
    int nClone = popSize - nCross - nMut;
    while (nClone < 1) {
        if (nCross >= nMut && nCross > 0)
            --nCross;
        else if (nMut > 0)
            --nMut;
        ++nClone;
    }

    const std::vector<double> probs = linearRankProbs(popSize, gaParams.rankPressure);
    std::discrete_distribution<int> rankDist(probs.begin(), probs.end());

    for (int gen = 1; gen <= gaParams.maxGenerations; ++gen) {
        std::vector<Individual> next;
        next.reserve(popSize);

        next.push_back(pop[0]);
        for (int i = 1; i < nClone; ++i)
            next.push_back(pop[rankDist(rng)]);

        for (int i = 0; i < nCross; ++i) {
            const Individual& a = pop[rankDist(rng)];
            const Individual& b = pop[rankDist(rng)];
            next.push_back(subtreeCrossover(a, b, tree, rng));
        }

        for (int i = 0; i < nMut; ++i) {
            Individual m = pop[rankDist(rng)];
            mutateForce(m, rng);
            next.push_back(m);
        }

        pop.swap(next);
        evaluateAll(pop, graph, prep, tree, evalParams);
        std::sort(pop.begin(), pop.end(), fitnessLess);

        result.generationsRun = gen;

        if (pop[0].fitness < bestFit - eps) {
            result.best = pop[0];
            bestFit = pop[0].fitness;
            noImprove = 0;
        } else {
            noImprove += 1;
        }

        if (noImprove >= gaParams.noImproveLimit) {
            result.stoppedByNoImprove = true;
            break;
        }
    }

    return result;
}

void printSchedule(const Individual& ind, const etg::ETG& graph, std::ostream& os) {
    os << "Fitness: " << ind.fitness << "\n";
    os << "Total cost: " << ind.schedule.totalCost << "\n";
    os << "Makespan: " << ind.schedule.makespan << "\n";
    os << "Valid: " << (ind.schedule.valid ? "yes" : "no") << "\n\n";

    for (const TaskAssignment& a : ind.schedule.assignments) {
        os << "T" << a.taskId << " (" << etg::categoryName(graph.tasks[a.taskId].cat) << ")"
           << " procs:";
        for (int p : a.procIds)
            os << " P" << p;
        os << " start=" << a.startTime
           << " finish=" << a.finishTime
           << " cost=" << a.cost << "\n";
    }
}

}
