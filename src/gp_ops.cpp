#include "gp_ops.h"
#include <algorithm>

namespace gp {

static int depthOf(const Node& root, const Node* target) {
    if (&root == target) return 0;
    if (!root.isFunc) return -1;
    int ld = depthOf(*root.left, target);
    if (ld >= 0) return 1 + ld;
    int rd = depthOf(*root.right, target);
    if (rd >= 0) return 1 + rd;
    return -1;
}

static Node* pickRandom(Node& root, std::mt19937& rng) {
    std::vector<Node*> nodes;
    collectNodes(root, nodes);
    std::uniform_int_distribution<int> dist(0, static_cast<int>(nodes.size()) - 1);
    return nodes[dist(rng)];
}

static const Node* pickRandomConst(const Node& root, std::mt19937& rng) {
    // Collect from a const ref by casting away const for read-only traversal,
    // returning a const pointer. The tree is not modified.
    std::vector<Node*> nodes;
    collectNodes(const_cast<Node&>(root), nodes);
    std::uniform_int_distribution<int> dist(0, static_cast<int>(nodes.size()) - 1);
    return nodes[dist(rng)];
}

// --- Subtree Crossover ---

std::unique_ptr<Node> subtreeCrossover(
    const Node& parentA, const Node& parentB,
    int maxDepth, std::mt19937& rng)
{
    auto child = clone(parentA);

    Node* crossPoint = pickRandom(*child, rng);
    const Node* donorSubtree = pickRandomConst(parentB, rng);
    auto donorClone = clone(*donorSubtree);

    // Replace the cross point's contents with the donor subtree.
    crossPoint->isFunc   = donorClone->isFunc;
    crossPoint->func     = donorClone->func;
    crossPoint->term     = donorClone->term;
    crossPoint->constVal = donorClone->constVal;
    crossPoint->left     = std::move(donorClone->left);
    crossPoint->right    = std::move(donorClone->right);

    if (depth(*child) > maxDepth)
        return clone(parentA);

    return child;
}

// --- Subtree Mutation ---

std::unique_ptr<Node> subtreeMutation(
    const Node& parent, int maxDepth, std::mt19937& rng)
{
    auto child = clone(parent);

    Node* mutPoint = pickRandom(*child, rng);

    int pointDepth = depthOf(*child, mutPoint);
    if (pointDepth < 0) pointDepth = 0;
    int subtreeMaxDepth = std::max(1, maxDepth - pointDepth);
    auto newSubtree = generateGrow(subtreeMaxDepth, rng);

    mutPoint->isFunc   = newSubtree->isFunc;
    mutPoint->func     = newSubtree->func;
    mutPoint->term     = newSubtree->term;
    mutPoint->constVal = newSubtree->constVal;
    mutPoint->left     = std::move(newSubtree->left);
    mutPoint->right    = std::move(newSubtree->right);

    if (depth(*child) > maxDepth)
        return clone(parent);

    return child;
}

// --- Point Mutation ---

std::unique_ptr<Node> pointMutation(const Node& parent, std::mt19937& rng) {
    auto child = clone(parent);

    std::vector<Node*> nodes;
    collectNodes(*child, nodes);
    if (nodes.empty()) return child;

    std::uniform_int_distribution<int> dist(0, static_cast<int>(nodes.size()) - 1);
    Node* target = nodes[dist(rng)];

    if (target->isFunc) {
        // Replace function with a different function.
        FuncType original = target->func;
        FuncType replacement;
        do {
            std::uniform_int_distribution<int> fd(0, FUNC_COUNT - 1);
            replacement = static_cast<FuncType>(fd(rng));
        } while (replacement == original && FUNC_COUNT > 1);
        target->func = replacement;
    } else {
        // Replace terminal with a different terminal.
        TermType original = target->term;
        TermType replacement;
        do {
            std::uniform_int_distribution<int> td(0, TERM_COUNT - 1);
            replacement = static_cast<TermType>(td(rng));
        } while (replacement == original && TERM_COUNT > 1);
        target->term = replacement;
        if (replacement == TermType::CONST) {
            std::uniform_real_distribution<double> cd(-2.0, 2.0);
            target->constVal = cd(rng);
        }
    }

    return child;
}

}
