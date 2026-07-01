#include "test_helpers.h"
#include "gp_ops.h"

using namespace gp;

TEST(test_crossover_depth_limit) {
    std::mt19937 rng(123);
    int maxDepth = 5;

    auto parentA = generateFull(4, rng);
    auto parentB = generateFull(4, rng);

    for (int i = 0; i < 100; ++i) {
        auto child = subtreeCrossover(*parentA, *parentB, maxDepth, rng);
        ASSERT_TRUE(child != nullptr);
        ASSERT_TRUE(depth(*child) <= maxDepth);
        ASSERT_TRUE(size(*child) >= 1);
    }
}

TEST(test_crossover_produces_valid_tree) {
    std::mt19937 rng(42);

    auto parentA = makeFunc(FuncType::ADD, makeTerm(TermType::COST), makeConst(-1.0));
    auto parentB = makeFunc(FuncType::MUL, makeTerm(TermType::TIME), makeConst(2.0));

    auto child = subtreeCrossover(*parentA, *parentB, 5, rng);

    EvalContext ctx;
    ctx.cost = 10.0; ctx.time = 20.0;
    double val = evaluate(*child, ctx);
    // Just check it computes without crashing and returns a finite value.
    ASSERT_TRUE(std::isfinite(val));
}

TEST(test_subtree_mutation_depth_limit) {
    std::mt19937 rng(99);
    int maxDepth = 5;

    auto parent = generateFull(4, rng);

    for (int i = 0; i < 100; ++i) {
        auto child = subtreeMutation(*parent, maxDepth, rng);
        ASSERT_TRUE(child != nullptr);
        ASSERT_TRUE(depth(*child) <= maxDepth);
    }
}

TEST(test_subtree_mutation_changes_tree) {
    std::mt19937 rng(77);

    auto parent = generateFull(3, rng);

    EvalContext ctx;
    ctx.cost = 5.0; ctx.time = 10.0; ctx.buyCost = 3.0;
    ctx.nSucc = 2.0; ctx.freeAt = 0.0;

    double parentVal = evaluate(*parent, ctx);

    // Over many attempts, at least one mutation should produce a different value.
    bool foundDifferent = false;
    for (int i = 0; i < 50; ++i) {
        auto child = subtreeMutation(*parent, 5, rng);
        double childVal = evaluate(*child, ctx);
        if (std::abs(childVal - parentVal) > 1e-12) {
            foundDifferent = true;
            break;
        }
    }
    ASSERT_TRUE(foundDifferent);
}

TEST(test_point_mutation_preserves_structure) {
    std::mt19937 rng(55);

    auto parent = makeFunc(FuncType::ADD,
        makeFunc(FuncType::MUL, makeTerm(TermType::COST), makeConst(1.0)),
        makeTerm(TermType::TIME));

    int parentSize = size(*parent);
    int parentDepth = depth(*parent);

    auto child = pointMutation(*parent, rng);

    // Point mutation does not change tree shape.
    ASSERT_EQ(size(*child), parentSize);
    ASSERT_EQ(depth(*child), parentDepth);
}

TEST(test_point_mutation_does_not_modify_parent) {
    std::mt19937 rng(33);

    auto parent = makeFunc(FuncType::ADD, makeTerm(TermType::COST), makeConst(5.0));

    EvalContext ctx;
    ctx.cost = 10.0;
    double beforeVal = evaluate(*parent, ctx);

    auto child = pointMutation(*parent, rng);
    (void)child;

    double afterVal = evaluate(*parent, ctx);
    ASSERT_NEAR(beforeVal, afterVal, 1e-12);
}

TEST(test_crossover_does_not_modify_parents) {
    std::mt19937 rng(11);

    auto parentA = makeFunc(FuncType::ADD, makeTerm(TermType::COST), makeConst(1.0));
    auto parentB = makeFunc(FuncType::MUL, makeTerm(TermType::TIME), makeConst(2.0));

    EvalContext ctx;
    ctx.cost = 10.0; ctx.time = 20.0;
    double aVal = evaluate(*parentA, ctx);
    double bVal = evaluate(*parentB, ctx);

    auto child = subtreeCrossover(*parentA, *parentB, 5, rng);
    (void)child;

    ASSERT_NEAR(evaluate(*parentA, ctx), aVal, 1e-12);
    ASSERT_NEAR(evaluate(*parentB, ctx), bVal, 1e-12);
}

TEST(test_crossover_single_leaf_parents) {
    std::mt19937 rng(42);
    auto a = makeConst(1.0);
    auto b = makeTerm(TermType::TIME);

    for (int i = 0; i < 20; ++i) {
        auto child = subtreeCrossover(*a, *b, 5, rng);
        ASSERT_TRUE(child != nullptr);
        ASSERT_TRUE(size(*child) >= 1);

        EvalContext ctx;
        ctx.time = 7.0;
        double val = evaluate(*child, ctx);
        ASSERT_TRUE(std::isfinite(val));
    }
}

TEST(test_subtree_mutation_single_leaf) {
    std::mt19937 rng(42);
    auto parent = makeConst(5.0);

    for (int i = 0; i < 20; ++i) {
        auto child = subtreeMutation(*parent, 3, rng);
        ASSERT_TRUE(child != nullptr);
        ASSERT_TRUE(depth(*child) <= 3);
    }
}

TEST(test_subtree_mutation_does_not_modify_parent) {
    std::mt19937 rng(88);
    auto parent = makeFunc(FuncType::ADD, makeTerm(TermType::COST), makeConst(5.0));

    EvalContext ctx;
    ctx.cost = 10.0;
    double beforeVal = evaluate(*parent, ctx);

    for (int i = 0; i < 20; ++i) {
        auto child = subtreeMutation(*parent, 5, rng);
        (void)child;
    }

    double afterVal = evaluate(*parent, ctx);
    ASSERT_NEAR(beforeVal, afterVal, 1e-12);
}

TEST(test_point_mutation_single_leaf) {
    std::mt19937 rng(42);
    auto parent = makeConst(3.0);

    auto child = pointMutation(*parent, rng);
    ASSERT_TRUE(child != nullptr);
    ASSERT_EQ(size(*child), 1);
    ASSERT_EQ(depth(*child), 0);
    ASSERT_FALSE(child->isFunc);
}

TEST(test_point_mutation_func_to_different_func) {
    std::mt19937 rng(42);
    auto parent = makeFunc(FuncType::ADD, makeTerm(TermType::COST), makeTerm(TermType::TIME));

    bool foundDiffFunc = false;
    for (int i = 0; i < 50; ++i) {
        auto child = pointMutation(*parent, rng);
        if (child->isFunc && child->func != FuncType::ADD) {
            foundDiffFunc = true;
            break;
        }
    }
    ASSERT_TRUE(foundDiffFunc);
}

TEST(test_crossover_evaluates_correctly) {
    std::mt19937 rng(7);
    auto a = makeFunc(FuncType::SUB, makeConst(10.0), makeTerm(TermType::COST));
    auto b = makeFunc(FuncType::MUL, makeTerm(TermType::TIME), makeConst(0.5));

    EvalContext ctx;
    ctx.cost = 3.0; ctx.time = 8.0;

    for (int i = 0; i < 30; ++i) {
        auto child = subtreeCrossover(*a, *b, 5, rng);
        double val = evaluate(*child, ctx);
        ASSERT_TRUE(std::isfinite(val));
    }
}

int main() {
    std::cout << "test_gp_ops\n";
    RUN(test_crossover_depth_limit);
    RUN(test_crossover_produces_valid_tree);
    RUN(test_crossover_single_leaf_parents);
    RUN(test_crossover_evaluates_correctly);
    RUN(test_subtree_mutation_depth_limit);
    RUN(test_subtree_mutation_changes_tree);
    RUN(test_subtree_mutation_single_leaf);
    RUN(test_subtree_mutation_does_not_modify_parent);
    RUN(test_point_mutation_preserves_structure);
    RUN(test_point_mutation_does_not_modify_parent);
    RUN(test_point_mutation_single_leaf);
    RUN(test_point_mutation_func_to_different_func);
    RUN(test_crossover_does_not_modify_parents);
    std::cout << "All tests passed.\n";
    return 0;
}
