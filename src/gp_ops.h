#ifndef GP_OPS_H
#define GP_OPS_H

#include "gp_tree.h"
#include <memory>
#include <random>

namespace gp {

std::unique_ptr<Node> subtreeCrossover(
    const Node& parentA, const Node& parentB,
    int maxDepth, std::mt19937& rng);

std::unique_ptr<Node> subtreeMutation(
    const Node& parent, int maxDepth, std::mt19937& rng);

std::unique_ptr<Node> pointMutation(
    const Node& parent, std::mt19937& rng);

}

#endif
