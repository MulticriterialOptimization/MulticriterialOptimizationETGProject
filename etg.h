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

// Task categories per the ETG specification. How a task may be executed:
// GT - one resource of any type (default when @type does not mention the task),
// DT - one SPECIALIZED resource (sentinels in the matrices may narrow it further to its "specific" dedicated resources),
// UT - one UNIVERSAL resource,
// CDT - `width` specialized resources working SIMULTANEOUSLY,
// CGT - `width` resources of any type working SIMULTANEOUSLY.
// Categories are read from the optional `@type` section:
//  @type
//  T3 DT
//  T5 CDT 3
//  T7 CGT 2
// Tasks absent from @type are GT.
// Independently, a sentinel value < 0 in @times/@cost marks a FORBIDDEN
// assignment of that task to that resource (used e.g. for "specific" DT).
// Task repetition from the ETG spec is NOT modeled. -> but maybe it should be -> CONSIDER / ASK??? 
enum class Category { GT, DT, UT, CDT, CGT };

const char* categoryName(Category c);

struct Task {
    int id = -1;
    int declaredSucc = 0;
    Category cat = Category::GT;
    int width = 1; // number of simultaneous resources (>1 only for CDT/CGT)
    std::vector<Edge> successors;
};

// @proc row interpretation (3 columns):
//   raw[0] - one-time purchase/activation cost, paid once iff the resource
//            executes at least one task,
//   raw[1] - reserved/unknown (always 0 in available instances) - ASK GÓRSKI.
//   raw[2] - type flag: 1 = universal (may execute many tasks sequentially),
//            0 = specialized (executes exactly ONE task, per the ETG spec).
// NOTE for input.txt: the fast/expensive columns of the matrices (0-1) do not
// coincide with the flag-0 rows (25-26). The algorithm does not depend on this:
// it only uses the numbers + the capacity rule implied by the flag.
struct Processor {
    int id = -1;
    std::vector<int> raw;

    int cost() const { return raw.empty() ? 0 : raw[0]; }
    int typeFlag() const { return raw.size() > 2 ? raw[2] : -1; }
    bool isUniversal() const { return typeFlag() != 0; } // missing flag -> assume universal
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

struct ValidationResult {
    std::vector<std::string> errors; 
    std::vector<std::string> warnings;
    bool ok() const { return errors.empty(); }
};

ValidationResult validateETG(const ETG& g);

void validateOrThrow(const ETG& g);

}

#endif