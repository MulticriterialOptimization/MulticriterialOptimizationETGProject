#include "test_helpers.h"

#include "etg.h"
#include "etg_prep.h"
#include "spanning_tree.h"
#include "schedule.h"
#include "ga.h"

#include <stdexcept>

// Tests for the GA layer: countPeTypes, linear rank selection, extended-scheme
// parameter validation and the runGaExtended loop (ETG_GA_Design_v2.md §13).

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

// T0 --(data=10)--> T1, sentinels force T0 onto P0 and T1 onto P1 (both
// specialized), so exactly one feasible schedule exists (cost 48, see
// test_evaluator.cpp for the hand computation).
etg::ETG makeCrossProcComm() {
    etg::ETG g;
    g.numTasks = 2;
    g.numProcs = 2;
    g.tasks = { task(0, etg::Category::GT, {{1, 10}}), task(1, etg::Category::GT, {}) };
    g.procs = { proc(0, 0, 0), proc(1, 0, 0) };
    g.times = { {5, -1}, {-1, 3} };
    g.costs = { {10, -1}, {-1, 8} };
    etg::CommChannel ch;
    ch.name = "CHAN";
    ch.connectCost = 15;
    ch.bandwidth = 7;
    ch.canConnect = {1, 1};
    g.channels = { ch };
    return g;
}

solver::EvalParams evalParams(int tmax) {
    solver::EvalParams p;
    p.tmax = tmax;
    return p;
}

solver::GaParams extendedParams(unsigned seed) {
    solver::GaParams gp;
    gp.useExtendedScheme = true;
    gp.alpha = 5.0;
    gp.beta = 0.6;
    gp.gamma = 0.3;
    gp.delta = 0.1;
    gp.noImproveLimit = 5;
    gp.maxGenerations = 50;
    gp.seed = seed;
    return gp;
}

} // namespace

TEST(test_count_pe_types) {
    etg::ETG mixed = makeCrossProcComm();          // two specialized procs
    ASSERT_EQ(solver::countPeTypes(mixed), 1);

    mixed.procs = { proc(0, 0, 1), proc(1, 0, 0) };  // universal + specialized
    ASSERT_EQ(solver::countPeTypes(mixed), 2);

    mixed.procs = { proc(0, 0, 1), proc(1, 0, 1) };  // only universal
    ASSERT_EQ(solver::countPeTypes(mixed), 1);
}

TEST(test_linear_rank_probs) {
    const int pop = 10;
    const double sp = 1.5;
    std::vector<double> p = solver::linearRankProbs(pop, sp);

    ASSERT_EQ(static_cast<int>(p.size()), pop);
    ASSERT_NEAR(p[0], sp / pop, 1e-12);              // best rank gets sp/POP
    ASSERT_NEAR(p[pop - 1], (2.0 - sp) / pop, 1e-12); // worst gets (2-sp)/POP

    double sum = 0.0;
    for (int i = 0; i < pop; ++i) {
        sum += p[i];
        if (i > 0)
            ASSERT_TRUE(p[i] <= p[i - 1]);           // non-increasing with rank
    }
    ASSERT_NEAR(sum, 1.0, 1e-12);
}

TEST(test_extended_params_validation) {
    solver::GaParams ok = extendedParams(1);
    solver::validateExtendedParams(ok);                // must not throw

    solver::GaParams bad = ok;
    bad.beta = 0.5;                                   // 0.5+0.3+0.1 != 1
    bool thrown = false;
    try {
        solver::validateExtendedParams(bad);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    ASSERT_TRUE(thrown);

    solver::GaParams badSp = ok;
    badSp.rankPressure = 3.0;
    thrown = false;
    try {
        solver::validateExtendedParams(badSp);
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    ASSERT_TRUE(thrown);
}

TEST(test_extended_deterministic_and_finds_solution) {
    etg::ETG g = makeCrossProcComm();
    etg::PreparedData pd = etg::prepare(g);
    solver::SpanningTree tree = solver::buildSpanningTree(pd);

    solver::GaParams gp = extendedParams(7);
    solver::GaResult r1 = solver::runGaExtended(g, pd, tree, gp, evalParams(0));
    solver::GaResult r2 = solver::runGaExtended(g, pd, tree, gp, evalParams(0));

    ASSERT_NEAR(r1.best.fitness, r2.best.fitness, 1e-9);
    ASSERT_EQ(r1.generationsRun, r2.generationsRun);

    // Only one feasible assignment exists, so the GA must find it.
    ASSERT_TRUE(r1.best.schedule.valid);
    ASSERT_NEAR(r1.best.schedule.totalCost, 48.0, 1e-9);
}

TEST(test_extended_dynamic_stop) {
    etg::ETG g = makeCrossProcComm();
    etg::PreparedData pd = etg::prepare(g);
    solver::SpanningTree tree = solver::buildSpanningTree(pd);

    solver::GaParams gp = extendedParams(3);
    gp.maxGenerations = 1000;

    solver::GaResult r = solver::runGaExtended(g, pd, tree, gp, evalParams(0));

    // The instance has a single feasible schedule, so fitness cannot improve
    // and the run must stop after noImproveLimit generations, not maxGenerations.
    ASSERT_TRUE(r.stoppedByNoImprove);
    ASSERT_EQ(r.generationsRun, gp.noImproveLimit);
}

int main() {
    std::cout << "test_ga\n";
    RUN(test_count_pe_types);
    RUN(test_linear_rank_probs);
    RUN(test_extended_params_validation);
    RUN(test_extended_deterministic_and_finds_solution);
    RUN(test_extended_dynamic_stop);
    std::cout << "All tests passed.\n";
    return 0;
}
