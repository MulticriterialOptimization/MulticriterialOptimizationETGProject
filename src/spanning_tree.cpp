#include "spanning_tree.h"

namespace solver {

SpanningTree buildSpanningTree(const etg::PreparedData& prep) {
    SpanningTree tree;
    tree.parent.assign(prep.allowed.size(), -1);

    for (int t = 0; t < static_cast<int>(prep.allowed.size()); ++t) {
        if (prep.preds[t].empty())
            continue;

        int best = prep.preds[t][0];
        for (int u : prep.preds[t])
            if (u < best)
                best = u;
        tree.parent[t] = best;
    }

    return tree;
}

}
