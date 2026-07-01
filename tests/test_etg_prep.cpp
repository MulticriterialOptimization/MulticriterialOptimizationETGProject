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

int main() {
    std::cout << "test_etg_prep\n";
    loadGraph();
    RUN(test_gt0_allowed_all);
    RUN(test_ut2_allowed_universal_only);
    RUN(test_cdt3_allowed_specialized_only);
    RUN(test_dt8_allowed_specialized_only);
    RUN(test_predecessors);
    RUN(test_topo_order_size);
    RUN(test_nsucc);
    std::cout << "All tests passed.\n";
    return 0;
}
