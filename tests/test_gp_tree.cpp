#include "test_helpers.h"
#include "gp_tree.h"

using namespace gp;

TEST(test_make_and_evaluate_simple) {
    // Tree: (+ (× cost -1.0) (× time -0.5))
    // With cost=10, time=20 => (10*-1.0) + (20*-0.5) = -10 + -10 = -20
    auto tree = makeFunc(FuncType::ADD,
        makeFunc(FuncType::MUL, makeTerm(TermType::COST), makeConst(-1.0)),
        makeFunc(FuncType::MUL, makeTerm(TermType::TIME), makeConst(-0.5)));

    EvalContext ctx;
    ctx.cost = 10.0;
    ctx.time = 20.0;

    double result = evaluate(*tree, ctx);
    ASSERT_NEAR(result, -20.0, 1e-9);
}

TEST(test_evaluate_div_by_zero) {
    // DIV protected: x / 0 => 1.0
    auto tree = makeFunc(FuncType::DIV, makeConst(42.0), makeConst(0.0));
    EvalContext ctx;
    double result = evaluate(*tree, ctx);
    ASSERT_NEAR(result, 1.0, 1e-9);
}

TEST(test_evaluate_min_max) {
    auto tree_min = makeFunc(FuncType::MIN, makeConst(3.0), makeConst(7.0));
    auto tree_max = makeFunc(FuncType::MAX, makeConst(3.0), makeConst(7.0));
    EvalContext ctx;
    ASSERT_NEAR(evaluate(*tree_min, ctx), 3.0, 1e-9);
    ASSERT_NEAR(evaluate(*tree_max, ctx), 7.0, 1e-9);
}

TEST(test_depth_simple) {
    auto leaf = makeTerm(TermType::COST);
    ASSERT_EQ(depth(*leaf), 0);

    auto tree = makeFunc(FuncType::ADD, makeTerm(TermType::COST), makeTerm(TermType::TIME));
    ASSERT_EQ(depth(*tree), 1);

    auto deep = makeFunc(FuncType::ADD,
        makeFunc(FuncType::MUL, makeTerm(TermType::COST), makeConst(1.0)),
        makeTerm(TermType::TIME));
    ASSERT_EQ(depth(*deep), 2);
}

TEST(test_size) {
    auto leaf = makeTerm(TermType::COST);
    ASSERT_EQ(size(*leaf), 1);

    // (+ cost time) = 3 nodes
    auto tree = makeFunc(FuncType::ADD, makeTerm(TermType::COST), makeTerm(TermType::TIME));
    ASSERT_EQ(size(*tree), 3);
}

TEST(test_clone_deep_copy) {
    auto original = makeFunc(FuncType::ADD,
        makeTerm(TermType::COST),
        makeConst(5.0));

    auto copy = clone(*original);

    // Modify the copy's constant.
    copy->right->constVal = 99.0;

    // Original must be unchanged.
    ASSERT_NEAR(original->right->constVal, 5.0, 1e-9);
    ASSERT_NEAR(copy->right->constVal, 99.0, 1e-9);
}

TEST(test_collect_nodes) {
    auto tree = makeFunc(FuncType::ADD,
        makeTerm(TermType::COST),
        makeFunc(FuncType::MUL, makeConst(1.0), makeConst(2.0)));

    std::vector<Node*> nodes;
    collectNodes(*tree, nodes);
    // ADD, COST, MUL, 1.0, 2.0 = 5 nodes
    ASSERT_EQ(static_cast<int>(nodes.size()), 5);
}

TEST(test_generate_full_depth) {
    std::mt19937 rng(42);
    for (int d = 1; d <= 5; ++d) {
        auto tree = generateFull(d, rng);
        ASSERT_EQ(depth(*tree), d);
    }
}

TEST(test_generate_grow_depth) {
    std::mt19937 rng(42);
    for (int i = 0; i < 50; ++i) {
        auto tree = generateGrow(4, rng);
        ASSERT_TRUE(depth(*tree) <= 4);
        ASSERT_TRUE(size(*tree) >= 1);
    }
}

TEST(test_ramped_half_and_half_count) {
    std::mt19937 rng(42);
    int popSize = 30;
    auto trees = rampedHalfAndHalf(popSize, 2, 5, rng);
    ASSERT_EQ(static_cast<int>(trees.size()), popSize);

    for (auto& t : trees) {
        ASSERT_TRUE(t != nullptr);
        ASSERT_TRUE(depth(*t) >= 0);
        ASSERT_TRUE(depth(*t) <= 5);
    }
}

TEST(test_terminal_values) {
    EvalContext ctx;
    ctx.cost = 1.0; ctx.time = 2.0; ctx.buyCost = 3.0;
    ctx.nSucc = 4.0; ctx.freeAt = 5.0;

    ASSERT_NEAR(evaluate(*makeTerm(TermType::COST), ctx), 1.0, 1e-9);
    ASSERT_NEAR(evaluate(*makeTerm(TermType::TIME), ctx), 2.0, 1e-9);
    ASSERT_NEAR(evaluate(*makeTerm(TermType::BUY_COST), ctx), 3.0, 1e-9);
    ASSERT_NEAR(evaluate(*makeTerm(TermType::N_SUCC), ctx), 4.0, 1e-9);
    ASSERT_NEAR(evaluate(*makeTerm(TermType::FREE_AT), ctx), 5.0, 1e-9);
    ASSERT_NEAR(evaluate(*makeConst(7.7), ctx), 7.7, 1e-9);
}

int main() {
    std::cout << "test_gp_tree\n";
    RUN(test_make_and_evaluate_simple);
    RUN(test_evaluate_div_by_zero);
    RUN(test_evaluate_min_max);
    RUN(test_depth_simple);
    RUN(test_size);
    RUN(test_clone_deep_copy);
    RUN(test_collect_nodes);
    RUN(test_generate_full_depth);
    RUN(test_generate_grow_depth);
    RUN(test_ramped_half_and_half_count);
    RUN(test_terminal_values);
    std::cout << "All tests passed.\n";
    return 0;
}
