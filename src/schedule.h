#ifndef SCHEDULE_H
#define SCHEDULE_H

#include "genes.h"
#include <vector>

namespace solver {

struct TaskAssignment {
    int taskId = -1;
    std::vector<int> procIds;
    double startTime  = 0.0;
    double finishTime = 0.0;
    double cost       = 0.0;
};

struct Schedule {
    std::vector<TaskAssignment> assignments;
    double totalCost = 0.0;
    double makespan  = 0.0;
    bool valid       = true;
};

struct Individual {
    std::vector<PeGene>  peGenes;
    std::vector<ClsGene> clsGenes;
    Schedule schedule;
    double fitness = 1e18;
};

}

#endif
