#include "test_helpers.h"

#include "etg.h"
#include "etg_prep.h"
#include "spanning_tree.h"
#include "schedule.h"
#include "genes.h"
#include "evaluator.h"
#include "ga.h"

// Tests for the constructive GA solver (evaluator + ga). The plan-level design
// is in ETG_GA_Design_v2.md. Every instance below is small enough to be
// hand-computed, so the assertions pin down exact cost/makespan values.

namespace {

etg::Processor proc(int id, int buyCost, int typeFlag) {
    etg::Processor p;
    p.id = id;
    p.raw = {buyCost, 0, typeFlag};
    return p;
}

etg::Task task(int id, etg::Category cat, std::vector<etg::Edge> succ) {
    etg::Task t;
    t.id = id;
    t.cat = cat;
    t.declaredSucc = static_cast<int>(succ.size());
    t.successors = std::move(succ);
    return t;
}

solver::Individual individual(int n, solver::PeGene pe, solver::ClsGene cls) {
    solver::Individual ind;
    ind.peGenes.assign(n, pe);
    ind.clsGenes.assign(n, cls);
    return ind;
}

// T0 --(precedence)--> T1, one universal processor. Any genotype must place
// both tasks on P0 (the only allowed processor).
etg::ETG makeChainOneProc() {
    etg::ETG g;
    g.numTasks = 2;
    g.numProcs = 1;
    g.tasks = { task(0, etg::Category::GT, {{1, 0}}), task(1, etg::Category::GT, {}) };
    g.procs = { proc(0, 0, 1) };
    g.times = { {5}, {3} };
    g.costs = { {10}, {8} };
    return g;
}

// T0 --(data=10)--> T1, but sentinels force T0 onto P0 and T1 onto P1 (both
// specialized). The transfer therefore crosses processors and uses the channel.
etg::ETG makeCrossProcComm() {
    etg::ETG g;
    g.numTasks = 2;
    g.numProcs = 2;
    g.tasks = { task(0, etg::Category::GT, {{1, 10}}), task(1, etg::Category::GT, {}) };
    g.procs = { proc(0, 0, 0), proc(1, 0, 0) };
    g.times = { {5, -1}, {-1, 3} };   // T0 only on P0, T1 only on P1
    g.costs = { {10, -1}, {-1, 8} };
    etg::CommChannel ch;
    ch.name = "CHAN";
    ch.connectCost = 15;
    ch.bandwidth = 7;
    ch.canConnect = {1, 1};
    g.channels = { ch };
    return g;
}

// T0 --(data=10)--> T1 on a single universal processor: same-processor
// transfer must be local (free, instantaneous).
etg::ETG makeLocalComm() {
    etg::ETG g;
    g.numTasks = 2;
    g.numProcs = 1;
    g.tasks = { task(0, etg::Category::GT, {{1, 10}}), task(1, etg::Category::GT, {}) };
    g.procs = { proc(0, 0, 1) };
    g.times = { {5}, {3} };
    g.costs = { {10}, {8} };
    etg::CommChannel ch;
    ch.name = "CHAN";
    ch.connectCost = 99;   // must never be charged (local transfer)
    ch.bandwidth = 1;
    ch.canConnect = {1};
    g.channels = { ch };
    return g;
}

solver::EvalParams evalParams(int tmax) {
    solver::EvalParams p;
    p.tmax = tmax;
    p.lambda = 1000.0;
    return p;
}

} // namespace

TEST(test_chain_cost_and_makespan) {
    etg::ETG g = makeChainOneProc();
    etg::PreparedData pd = etg::prepare(g);
    solver::SpanningTree tree = solver::buildSpanningTree(pd);

    // Result must be identical for any gene: only P0 is allowed.
    solver::Individual ind = individual(2, solver::PeGene::Cheapest, solver::ClsGene::ClsCheapest);
    solver::evaluateIndividual(ind, g, pd, tree, evalParams(0));

    ASSERT_TRUE(ind.schedule.valid);
    ASSERT_NEAR(ind.schedule.totalCost, 18.0, 1e-9);  // 10 + 8, buyCost 0
    ASSERT_NEAR(ind.schedule.makespan, 8.0, 1e-9);    // 5 then 3, sequential
    ASSERT_NEAR(ind.fitness, 18.0, 1e-9);
}

TEST(test_chain_gene_invariance) {
    etg::ETG g = makeChainOneProc();
    etg::PreparedData pd = etg::prepare(g);
    solver::SpanningTree tree = solver::buildSpanningTree(pd);

    // Every PE gene must yield the same schedule when only one processor exists.
    for (int gi = 0; gi < solver::peGeneCount(); ++gi) {
        solver::Individual ind = individual(
            2, static_cast<solver::PeGene>(gi), solver::ClsGene::ClsCheapest);
        solver::evaluateIndividual(ind, g, pd, tree, evalParams(0));
        ASSERT_TRUE(ind.schedule.valid);
        ASSERT_NEAR(ind.schedule.totalCost, 18.0, 1e-9);
        ASSERT_NEAR(ind.schedule.makespan, 8.0, 1e-9);
    }
}

TEST(test_cross_proc_communication) {
    etg::ETG g = makeCrossProcComm();
    etg::PreparedData pd = etg::prepare(g);
    solver::SpanningTree tree = solver::buildSpanningTree(pd);

    solver::Individual ind = individual(2, solver::PeGene::Cheapest, solver::ClsGene::ClsCheapest);
    solver::evaluateIndividual(ind, g, pd, tree, evalParams(0));

    // T0: start 0, finish 5, cost 10.
    // T1: dataReady = 5 + ceil(10/7)=2 -> start 7, finish 10, exec cost 8,
    //     connection cost = 15 (P0) + 15 (P1) = 30, so T1 cost = 38.
    ASSERT_TRUE(ind.schedule.valid);
    ASSERT_NEAR(ind.schedule.makespan, 10.0, 1e-9);
    ASSERT_NEAR(ind.schedule.totalCost, 48.0, 1e-9);  // 10 + 38
}

TEST(test_local_communication_is_free) {
    etg::ETG g = makeLocalComm();
    etg::PreparedData pd = etg::prepare(g);
    solver::SpanningTree tree = solver::buildSpanningTree(pd);

    solver::Individual ind = individual(2, solver::PeGene::Cheapest, solver::ClsGene::ClsCheapest);
    solver::evaluateIndividual(ind, g, pd, tree, evalParams(0));

    // Same processor -> no channel cost, no transfer time despite data=10.
    ASSERT_TRUE(ind.schedule.valid);
    ASSERT_NEAR(ind.schedule.makespan, 8.0, 1e-9);
    ASSERT_NEAR(ind.schedule.totalCost, 18.0, 1e-9);
}

TEST(test_tmax_penalty) {
    etg::ETG g = makeChainOneProc();
    etg::PreparedData pd = etg::prepare(g);
    solver::SpanningTree tree = solver::buildSpanningTree(pd);

    // makespan is 8. With Tmax=5 and lambda=1000: fitness = 18 + 1000*(8-5).
    solver::Individual over = individual(2, solver::PeGene::Cheapest, solver::ClsGene::ClsCheapest);
    solver::evaluateIndividual(over, g, pd, tree, evalParams(5));
    ASSERT_NEAR(over.fitness, 18.0 + 1000.0 * 3.0, 1e-9);
    ASSERT_TRUE(over.fitness > over.schedule.totalCost);

    // With Tmax=100 the schedule is feasible, so fitness == cost.
    solver::Individual ok = individual(2, solver::PeGene::Cheapest, solver::ClsGene::ClsCheapest);
    solver::evaluateIndividual(ok, g, pd, tree, evalParams(100));
    ASSERT_NEAR(ok.fitness, ok.schedule.totalCost, 1e-9);
}

TEST(test_evaluation_is_deterministic) {
    etg::ETG g = makeCrossProcComm();
    etg::PreparedData pd = etg::prepare(g);
    solver::SpanningTree tree = solver::buildSpanningTree(pd);

    solver::Individual a = individual(2, solver::PeGene::MinTS, solver::ClsGene::ClsHighestBw);
    solver::Individual b = individual(2, solver::PeGene::MinTS, solver::ClsGene::ClsHighestBw);
    solver::evaluateIndividual(a, g, pd, tree, evalParams(0));
    solver::evaluateIndividual(b, g, pd, tree, evalParams(0));

    ASSERT_NEAR(a.fitness, b.fitness, 1e-9);
    ASSERT_NEAR(a.schedule.totalCost, b.schedule.totalCost, 1e-9);
    ASSERT_NEAR(a.schedule.makespan, b.schedule.makespan, 1e-9);
}

TEST(test_ga_is_deterministic_for_fixed_seed) {
    etg::ETG g = makeCrossProcComm();
    etg::PreparedData pd = etg::prepare(g);
    solver::SpanningTree tree = solver::buildSpanningTree(pd);

    solver::GaParams gp;
    gp.populationSize = 20;
    gp.generations = 10;
    gp.seed = 123;

    solver::GaResult r1 = solver::runGa(g, pd, tree, gp, evalParams(0));
    solver::GaResult r2 = solver::runGa(g, pd, tree, gp, evalParams(0));

    // Same seed -> identical best solution.
    ASSERT_NEAR(r1.best.fitness, r2.best.fitness, 1e-9);
    ASSERT_NEAR(r1.best.schedule.totalCost, r2.best.schedule.totalCost, 1e-9);
    ASSERT_NEAR(r1.best.schedule.makespan, r2.best.schedule.makespan, 1e-9);

    // Only one feasible assignment exists (sentinels), so the GA must find it.
    ASSERT_TRUE(r1.best.schedule.valid);
    ASSERT_NEAR(r1.best.schedule.totalCost, 48.0, 1e-9);
}

int main() {
    std::cout << "test_evaluator\n";
    RUN(test_chain_cost_and_makespan);
    RUN(test_chain_gene_invariance);
    RUN(test_cross_proc_communication);
    RUN(test_local_communication_is_free);
    RUN(test_tmax_penalty);
    RUN(test_evaluation_is_deterministic);
    RUN(test_ga_is_deterministic_for_fixed_seed);
    std::cout << "All tests passed.\n";
    return 0;
}
