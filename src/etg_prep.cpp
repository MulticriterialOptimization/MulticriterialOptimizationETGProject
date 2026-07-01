#include "etg_prep.h"
#include <stdexcept>

namespace etg {

PreparedData prepare(const ETG& g) {
    PreparedData d;

    d.allowed.resize(g.numTasks);
    for (int t = 0; t < g.numTasks; ++t)
        for (int p = 0; p < g.numProcs; ++p)
            if (assignmentAllowed(g, t, p))
                d.allowed[t].push_back(p);

    d.preds = buildPredecessors(g);

    bool acyclic = true;
    d.topo = topoOrder(g, acyclic);
    if (!acyclic)
        throw std::runtime_error("prepare: graph contains a cycle");

    d.nSucc.resize(g.numTasks);
    for (int t = 0; t < g.numTasks; ++t)
        d.nSucc[t] = static_cast<int>(g.tasks[t].successors.size());

    return d;
}

}
