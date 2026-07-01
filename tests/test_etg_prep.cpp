#include "test_helpers.h"
#include "etg.h"
#include "etg_prep.h"
#include <algorithm>

static etg::ETG g;
static etg::PreparedData pd;

static void loadGraph() {
    g = etg::parseETG("input.txt");
    etg::validateOrThrow(g);
    pd = etg::prepare(g);
}

TEST(test_gt0_allowed_all) {
    // GT0 is a general task: any processor with non-negative sentinels is allowed.
    // input.txt times row 0: 30 10 3 4 (all >= 0), costs row 0: 3 2 50 10 (all >= 0)
    auto& a = pd.allowed[0];
    ASSERT_EQ(static_cast<int>(a.size()), 4);
    for (int p = 0; p < 4; ++p)
        ASSERT_TRUE(std::find(a.begin(), a.end(), p) != a.end());
}

TEST(test_ut2_allowed_universal_only) {
    // UT2 is a universal task: only universal processors (P0, P1) allowed.
    // Sentinels: times[2] = {20, 10, -1, -1} -> P2,P3 blocked by sentinel anyway.
    auto& a = pd.allowed[2];
    ASSERT_EQ(static_cast<int>(a.size()), 2);
    ASSERT_TRUE(std::find(a.begin(), a.end(), 0) != a.end());
    ASSERT_TRUE(std::find(a.begin(), a.end(), 1) != a.end());
}

TEST(test_cdt3_allowed_specialized_only) {
    // CDT3 is a common dedicated task: only specialized processors (P2, P3).
    // Sentinels: times[3] = {-1, -1, 1, 2} -> P0,P1 blocked by sentinel too.
    auto& a = pd.allowed[3];
    ASSERT_EQ(static_cast<int>(a.size()), 2);
    ASSERT_TRUE(std::find(a.begin(), a.end(), 2) != a.end());
    ASSERT_TRUE(std::find(a.begin(), a.end(), 3) != a.end());
}

TEST(test_dt8_allowed_specialized_only) {
    // DT8 is a dedicated task: only specialized processors.
    // Sentinels: times[8] = {-1, -1, 2, 4} -> P0,P1 blocked.
    auto& a = pd.allowed[8];
    ASSERT_EQ(static_cast<int>(a.size()), 2);
    ASSERT_TRUE(std::find(a.begin(), a.end(), 2) != a.end());
    ASSERT_TRUE(std::find(a.begin(), a.end(), 3) != a.end());
}

TEST(test_predecessors) {
    // GT0 has successors 1 and 2, so preds[1] contains 0 and preds[2] contains 0.
    ASSERT_TRUE(std::find(pd.preds[1].begin(), pd.preds[1].end(), 0) != pd.preds[1].end());
    ASSERT_TRUE(std::find(pd.preds[2].begin(), pd.preds[2].end(), 0) != pd.preds[2].end());
    // GT0 is a root, no predecessors.
    ASSERT_TRUE(pd.preds[0].empty());
}

TEST(test_topo_order_size) {
    ASSERT_EQ(static_cast<int>(pd.topo.size()), g.numTasks);
}

TEST(test_nsucc) {
    // GT0 has 2 successors (1 and 2).
    ASSERT_EQ(pd.nSucc[0], 2);
    // CGT6 has 0 successors (leaf).
    ASSERT_EQ(pd.nSucc[6], 0);
}

TEST(test_cgt4_allowed_all_types) {
    // CGT4 allows any processor type. times[4]={30,15,4,10} all >=0, costs[4]={3,2,70,30} all >=0.
    auto& a = pd.allowed[4];
    ASSERT_EQ(static_cast<int>(a.size()), 4);
    for (int p = 0; p < 4; ++p)
        ASSERT_TRUE(std::find(a.begin(), a.end(), p) != a.end());
}

TEST(test_cgt9_allowed_all_types) {
    // CGT9 allows any processor type. times[9]={10,5,3,4}, costs[9]={3,1,40,12}.
    auto& a = pd.allowed[9];
    ASSERT_EQ(static_cast<int>(a.size()), 4);
    for (int p = 0; p < 4; ++p)
        ASSERT_TRUE(std::find(a.begin(), a.end(), p) != a.end());
}

TEST(test_gt5_allowed_all) {
    // GT5 is general: any type, times[5]={50,30,5,5}, costs[5]={5,3,80,15} all >=0.
    auto& a = pd.allowed[5];
    ASSERT_EQ(static_cast<int>(a.size()), 4);
}

TEST(test_topo_order_valid) {
    // Every predecessor of task t must appear before t in topo order.
    std::vector<int> pos(g.numTasks, -1);
    for (int i = 0; i < static_cast<int>(pd.topo.size()); ++i)
        pos[pd.topo[i]] = i;

    for (int t = 0; t < g.numTasks; ++t) {
        ASSERT_TRUE(pos[t] >= 0); // every task is in the order
        for (int pred : pd.preds[t])
            ASSERT_TRUE(pos[pred] < pos[t]);
    }
}

TEST(test_all_allowed_have_valid_sentinels) {
    for (int t = 0; t < g.numTasks; ++t) {
        for (int p : pd.allowed[t]) {
            ASSERT_TRUE(g.times[t][p] >= 0);
            ASSERT_TRUE(g.costs[t][p] >= 0);
        }
    }
}

TEST(test_every_task_has_at_least_one_allowed) {
    for (int t = 0; t < g.numTasks; ++t)
        ASSERT_TRUE(!pd.allowed[t].empty());
}

TEST(test_predecessors_complete) {
    // UT2 has successors 9, 4, 6. So preds[9], preds[4], preds[6] should contain 2.
    ASSERT_TRUE(std::find(pd.preds[9].begin(), pd.preds[9].end(), 2) != pd.preds[9].end());
    ASSERT_TRUE(std::find(pd.preds[4].begin(), pd.preds[4].end(), 2) != pd.preds[4].end());
    ASSERT_TRUE(std::find(pd.preds[6].begin(), pd.preds[6].end(), 2) != pd.preds[6].end());
    // CDT3 has successors 7, 9. So preds[7] contains 3, preds[9] contains 3.
    ASSERT_TRUE(std::find(pd.preds[7].begin(), pd.preds[7].end(), 3) != pd.preds[7].end());
    ASSERT_TRUE(std::find(pd.preds[9].begin(), pd.preds[9].end(), 3) != pd.preds[9].end());
}

TEST(test_nsucc_all_tasks) {
    // Verify nSucc for every task matches the actual successor list length.
    for (int t = 0; t < g.numTasks; ++t)
        ASSERT_EQ(pd.nSucc[t], static_cast<int>(g.tasks[t].successors.size()));
}

int main() {
    std::cout << "test_etg_prep\n";
    loadGraph();
    RUN(test_gt0_allowed_all);
    RUN(test_ut2_allowed_universal_only);
    RUN(test_cdt3_allowed_specialized_only);
    RUN(test_dt8_allowed_specialized_only);
    RUN(test_cgt4_allowed_all_types);
    RUN(test_cgt9_allowed_all_types);
    RUN(test_gt5_allowed_all);
    RUN(test_predecessors);
    RUN(test_predecessors_complete);
    RUN(test_topo_order_size);
    RUN(test_topo_order_valid);
    RUN(test_all_allowed_have_valid_sentinels);
    RUN(test_every_task_has_at_least_one_allowed);
    RUN(test_nsucc);
    RUN(test_nsucc_all_tasks);
    std::cout << "All tests passed.\n";
    return 0;
}
