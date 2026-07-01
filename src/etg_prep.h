#ifndef ETG_PREP_H
#define ETG_PREP_H

#include "etg.h"
#include <vector>

namespace etg {

struct PreparedData {
    std::vector<std::vector<int>> allowed; // allowed[t] = list of feasible processors for task t
    std::vector<std::vector<int>> preds;   // preds[t] = predecessor task ids
    std::vector<int> topo;                 // topological order of task ids
    std::vector<int> nSucc;                // nSucc[t] = number of direct successors (GP terminal)
};

PreparedData prepare(const ETG& g);

}

#endif
