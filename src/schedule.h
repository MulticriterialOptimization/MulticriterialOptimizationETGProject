#ifndef SCHEDULE_H
#define SCHEDULE_H

#include "gp_tree.h"
#include <vector>
#include <memory>

namespace solver {

struct TaskAssignment {
    int taskId = -1;
    std::vector<int> procIds;   // 1 element for GT/DT/UT, >= 1 for CDT/CGT
    double startTime  = 0.0;
    double finishTime = 0.0;
    double cost       = 0.0;
};

struct Schedule {
    std::vector<TaskAssignment> assignments; // indexed by task id
    double totalCost = 0.0;                  // tasks + resource purchases + communication
    double makespan  = 0.0;                  // max finish time across all tasks
};

struct Individual {
    std::unique_ptr<gp::Node> tree;  // genotype (priority rule)
    Schedule schedule;                // phenotype (filled by decoder)
    double fitness = 1e18;            // lower is better
};

}

#endif
