#include "ga.h"

#include "genes.h"

#include <algorithm>
#include <iostream>

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

void crossover(const Individual& a, const Individual& b, Individual& child,
               std::mt19937& rng)
{
    child.peGenes = a.peGenes;
    child.clsGenes = a.clsGenes;

    std::uniform_real_distribution<double> coin(0.0, 1.0);
    for (int t = 0; t < static_cast<int>(child.peGenes.size()); ++t) {
        if (coin(rng) < 0.5) {
            child.peGenes[t] = b.peGenes[t];
            child.clsGenes[t] = b.clsGenes[t];
        }
    }
}

void mutate(Individual& ind, double mutationRate, std::mt19937& rng) {
    if (ind.peGenes.empty())
        return;

    std::uniform_real_distribution<double> coin(0.0, 1.0);
    std::uniform_int_distribution<int> taskDist(0, static_cast<int>(ind.peGenes.size()) - 1);

    if (coin(rng) < mutationRate)
        ind.peGenes[taskDist(rng)] = randomPeGene(rng);
    if (coin(rng) < mutationRate)
        ind.clsGenes[taskDist(rng)] = randomClsGene(rng);
}

int tournamentPick(const std::vector<Individual>& pop, int tournamentSize,
                   std::mt19937& rng)
{
    std::uniform_int_distribution<int> dist(0, static_cast<int>(pop.size()) - 1);
    int best = dist(rng);
    for (int i = 1; i < tournamentSize; ++i) {
        int idx = dist(rng);
        if (pop[idx].fitness < pop[best].fitness)
            best = idx;
    }
    return best;
}

void evaluateAll(std::vector<Individual>& pop, const etg::ETG& graph,
                 const etg::PreparedData& prep, const SpanningTree& tree,
                 const EvalParams& evalParams)
{
    for (Individual& ind : pop)
        evaluateIndividual(ind, graph, prep, tree, evalParams);
}

} // namespace

GaResult runGa(
    const etg::ETG& graph,
    const etg::PreparedData& prep,
    const SpanningTree& tree,
    const GaParams& gaParams,
    const EvalParams& evalParams)
{
    std::mt19937 rng(gaParams.seed);

    std::vector<Individual> pop;
    pop.reserve(gaParams.populationSize);
    for (int i = 0; i < gaParams.populationSize; ++i)
        pop.push_back(makeRandomIndividual(graph.numTasks, rng));

    evaluateAll(pop, graph, prep, tree, evalParams);

    GaResult result;
    result.generationsRun = 0;

    for (int gen = 0; gen < gaParams.generations; ++gen) {
        std::vector<Individual> sorted = pop;
        std::sort(sorted.begin(), sorted.end(),
                  [](const Individual& a, const Individual& b) {
                      return a.fitness < b.fitness;
                  });

        if (gen == 0 || sorted[0].fitness < result.best.fitness)
            result.best = sorted[0];

        std::vector<Individual> next;
        next.reserve(gaParams.populationSize);

        for (int i = 0; i < gaParams.eliteCount && i < static_cast<int>(sorted.size()); ++i)
            next.push_back(sorted[i]);

        std::uniform_real_distribution<double> coin(0.0, 1.0);

        while (static_cast<int>(next.size()) < gaParams.populationSize) {
            int pa = tournamentPick(pop, gaParams.tournamentSize, rng);
            int pb = tournamentPick(pop, gaParams.tournamentSize, rng);

            Individual child;
            if (coin(rng) < gaParams.crossoverRate)
                crossover(pop[pa], pop[pb], child, rng);
            else
                child = pop[pa];

            mutate(child, gaParams.mutationRate, rng);
            next.push_back(child);
        }

        pop.swap(next);
        evaluateAll(pop, graph, prep, tree, evalParams);
        result.generationsRun = gen + 1;
    }

    std::sort(pop.begin(), pop.end(),
              [](const Individual& a, const Individual& b) {
                  return a.fitness < b.fitness;
              });
    if (pop[0].fitness < result.best.fitness)
        result.best = pop[0];

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
