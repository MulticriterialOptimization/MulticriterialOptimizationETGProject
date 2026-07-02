#ifndef SPANNING_TREE_H
#define SPANNING_TREE_H

#include "etg_prep.h"
#include <vector>

namespace solver {

struct SpanningTree {
    std::vector<int> parent; // parent[taskId], -1 = root
};

SpanningTree buildSpanningTree(const etg::PreparedData& prep);

}

#endif
