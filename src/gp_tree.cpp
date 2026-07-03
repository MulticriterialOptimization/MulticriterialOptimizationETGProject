#include "gp_tree.h"
#include <algorithm>
#include <cmath>

namespace gp {

// Creates a binary function node owning two child subtrees
std::unique_ptr<Node> makeFunc(FuncType f, std::unique_ptr<Node> l, std::unique_ptr<Node> r) {
    auto n = std::make_unique<Node>();
    n->isFunc = true;
    n->func = f;
    n->left = std::move(l);
    n->right = std::move(r);
    return n;
}

// Creates a named terminal leaf
std::unique_ptr<Node> makeTerm(TermType t) {
    auto n = std::make_unique<Node>();
    n->isFunc = false;
    n->term = t;
    return n;
}

// Creates a constant-value leaf
std::unique_ptr<Node> makeConst(double val) {
    auto n = std::make_unique<Node>();
    n->isFunc = false;
    n->term = TermType::CONST;
    n->constVal = val;
    return n;
}

// Returns the numeric value of a terminal leaf given the current context
static double terminalValue(const Node& node, const EvalContext& ctx) {
    switch (node.term) {
        case TermType::COST:     return ctx.cost;
        case TermType::TIME:     return ctx.time;
        case TermType::BUY_COST: return ctx.buyCost;
        case TermType::N_SUCC:   return ctx.nSucc;
        case TermType::FREE_AT:  return ctx.freeAt;
        case TermType::CONST:    return node.constVal;
        default:                 return 0.0;
    }
}

static constexpr double DIV_PROTECT_EPS = 1e-9;

// Recursively evaluates the priority-rule tree for one (task, processor) candidate
double evaluate(const Node& node, const EvalContext& ctx) {
    if (!node.isFunc)
        return terminalValue(node, ctx);

    double l = evaluate(*node.left, ctx);
    double r = evaluate(*node.right, ctx);

    switch (node.func) {
        case FuncType::ADD: return l + r;
        case FuncType::SUB: return l - r;
        case FuncType::MUL: return l * r;
        case FuncType::DIV:
            return (std::abs(r) < DIV_PROTECT_EPS) ? 1.0 : l / r;
        case FuncType::MIN: return std::min(l, r);
        case FuncType::MAX: return std::max(l, r);
        default: return 0.0;
    }
}

// Returns the maximum depth of the tree
int depth(const Node& node) {
    if (!node.isFunc) return 0;
    return 1 + std::max(depth(*node.left), depth(*node.right));
}

// Returns the total number of nodes in the tree
int size(const Node& node) {
    if (!node.isFunc) return 1;
    return 1 + size(*node.left) + size(*node.right);
}

// Returns a deep copy of the entire subtree rooted at node
std::unique_ptr<Node> clone(const Node& node) {
    auto n = std::make_unique<Node>();
    n->isFunc = node.isFunc;
    n->func = node.func;
    n->term = node.term;
    n->constVal = node.constVal;
    if (node.left)  n->left = clone(*node.left);
    if (node.right) n->right = clone(*node.right);
    return n;
}

// Appends pointers to every node in the subtree (pre-order traversal) into out
void collectNodes(Node& root, std::vector<Node*>& out) {
    out.push_back(&root);
    if (root.left)  collectNodes(*root.left, out);
    if (root.right) collectNodes(*root.right, out);
}

// Generates a random terminal leaf
static std::unique_ptr<Node> randomTerminal(std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(0, TERM_COUNT - 1);
    auto t = static_cast<TermType>(dist(rng));
    if (t == TermType::CONST) {
        std::uniform_real_distribution<double> cdist(-2.0, 2.0);
        return makeConst(cdist(rng));
    }
    return makeTerm(t);
}

static FuncType randomFunc(std::mt19937& rng) {
    std::uniform_int_distribution<int> dist(0, FUNC_COUNT - 1);
    return static_cast<FuncType>(dist(rng));
}

// Recursive helper for generateFull
static std::unique_ptr<Node> generateFullImpl(int currentDepth, int maxDepth, std::mt19937& rng) {
    if (currentDepth >= maxDepth)
        return randomTerminal(rng);

    FuncType f = randomFunc(rng);
    auto left  = generateFullImpl(currentDepth + 1, maxDepth, rng);
    auto right = generateFullImpl(currentDepth + 1, maxDepth, rng);
    return makeFunc(f, std::move(left), std::move(right));
}

// Generates a full tree where every leaf sits at exactly maxDepth
std::unique_ptr<Node> generateFull(int maxDepth, std::mt19937& rng) {
    if (maxDepth <= 0) return randomTerminal(rng);
    return generateFullImpl(0, maxDepth, rng);
}

// Recursive helper for generateGrow
static std::unique_ptr<Node> generateGrowImpl(int currentDepth, int maxDepth, std::mt19937& rng) {
    if (currentDepth >= maxDepth)
        return randomTerminal(rng);

    int totalChoices = FUNC_COUNT + TERM_COUNT;
    std::uniform_int_distribution<int> dist(0, totalChoices - 1);
    int choice = dist(rng);

    if (choice < FUNC_COUNT) {
        auto f = static_cast<FuncType>(choice);
        auto left  = generateGrowImpl(currentDepth + 1, maxDepth, rng);
        auto right = generateGrowImpl(currentDepth + 1, maxDepth, rng);
        return makeFunc(f, std::move(left), std::move(right));
    }
    return randomTerminal(rng);
}

// Generates a grow tree where branches may stop before maxDepth
std::unique_ptr<Node> generateGrow(int maxDepth, std::mt19937& rng) {
    if (maxDepth <= 0) return randomTerminal(rng);
    return generateGrowImpl(0, maxDepth, rng);
}

// Ramped half-and-half: alternate full and grow trees across depths
std::vector<std::unique_ptr<Node>> rampedHalfAndHalf(
    int popSize, int minDepth, int maxDepth, std::mt19937& rng)
{
    if (minDepth > maxDepth) std::swap(minDepth, maxDepth);
    if (minDepth < 0) minDepth = 0;

    std::vector<std::unique_ptr<Node>> trees;
    trees.reserve(popSize);

    int depthRange = maxDepth - minDepth + 1;
    for (int i = 0; i < popSize; ++i) {
        int d = minDepth + (i % depthRange);
        if ((i / depthRange) % 2 == 0)
            trees.push_back(generateFull(d, rng));
        else
            trees.push_back(generateGrow(d, rng));
    }
    return trees;
}

}
