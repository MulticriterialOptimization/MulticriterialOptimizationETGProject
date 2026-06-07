#ifndef ETG_H
#define ETG_H

#include <string>
#include <vector>
#include <iosfwd>

namespace etg {

struct Edge {
    int to = -1;
    int data = 0; // volume of data to transfer; 0 = no communication needed
};

// UNCERTAIN: task category (DT / GT / UT / CDT / CGT) is not present in the current file format. 
// In this instance every task has finite times/costs on all processors,
// so all tasks behave as GT. A full ETG would need either an explicit category field
// per task or sentinel values in the times/cost matrices marking forbidden assignments.
// For CDT/CGT (parallel execution) the number of simultaneous resources per task is
// also missing. Task repetition, mentioned in the ETG spec, is not encoded either.
struct Task {
    int id = -1;
    int declaredSucc = 0;
    std::vector<Edge> successors;
};

struct Processor {
    int id = -1;
    std::vector<int> raw;

    // UNCERTAIN: likely a one-time activation cost paid once when this processor is used in the schedule? 
    int cost() const { return raw.empty() ? 0 : raw[0]; }

    // UNCERTAIN: raw[1] have no idea what is this? 

    // UNCERTAIN: hypothesis that raw[2] encodes resource type: 1 = universal
    // (slow, cheap), 0 = specialized (fast, expensive) ?
    int typeFlag() const { return raw.size() > 2 ? raw[2] : -1; }

    // SCHEDULING CONSTRAINT (pending confirmation of raw[2] semantics):
    // specialized resources can execute only one task in the entire schedule,
    // while universal resources can execute multiple tasks sequentially.
};

struct CommChannel {
    std::string name;
    int connectCost = 0;
    int bandwidth = 0;
    std::vector<int> canConnect; // per-processor: 1 = this processor can use the channel
};

struct ETG {
    int numTasks = 0;
    int numProcs = 0;
    std::vector<Task> tasks; // index == task id
    std::vector<Processor> procs; // index == processor id
    std::vector<std::vector<int>> times; // times[task][proc] = execution time
    std::vector<std::vector<int>> costs; // costs[task][proc] = execution cost
    std::vector<CommChannel> channels;

    // NOTE: the makespan time limit (hard constraint for the optimizer) is not part
    // of the ETG file and must be supplied separately (e.g. as a program argument) !!
};

ETG parseETG(const std::string& path);

// Predecessor lists for each task, derived from the successor graph.
std::vector<std::vector<int>> buildPredecessors(const ETG& g);

// Topological sort (Kahn). If the result has fewer than numTasks elements, the graph has a cycle.
std::vector<int> topoOrder(const ETG& g, bool& acyclic);

void printSummary(const ETG& g, std::ostream& os);

}

#endif