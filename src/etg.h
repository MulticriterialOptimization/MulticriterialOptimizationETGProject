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
// GT  - one resource of any type,
// DT  - one SPECIALIZED resource (sentinels in the matrices may narrow it
//       further to its "specific" dedicated resources),
// UT  - one UNIVERSAL resource,
// CDT - several SPECIALIZED resources working SIMULTANEOUSLY,
// CGT - several resources of any type working SIMULTANEOUSLY.
//
// The category is encoded directly in the task id prefix in the @tasks
// section (there is NO @type section anymore):
//   GT0   -> task 0, category GT
//   UT2   -> task 2, category UT
//   CDT3  -> task 3, category CDT
//   CGT4  -> task 4, category CGT
//   DT8   -> task 8, category DT
// A bare numeric prefix "T0" is also accepted and treated as GT, so older
// files still parse.
//
// IMPORTANT - common tasks (CDT/CGT) have NO fixed number of resources.
// How many resources k execute a common task is chosen by the optimizer
// (the genetic algorithm), not by the input file. The file only declares
// the category; k is part of the solution, not of the instance.
//
// Cost/time model for a common task run on a set S of k resources (k = |S|):
//   - each chosen resource pi does a 1/k share of the task, so it spends
//     time[task][pi] / k time and cost[task][pi] / k cost;
//   - the task's total time  = sum over pi in S of ( time[task][pi] / k );
//   - the task's total cost  = sum over pi in S of ( cost[task][pi] / k ).
//   Because the resources may differ, picking faster/cheaper resources for
//   the share lets the total time/cost shrink slightly relative to a single
//   resource. (For a non-common task, k = 1 and both formulas collapse to
//   the plain matrix entry.)
//
// Communication out of a common task: the result is produced in k pieces,
// one per participating resource. For every outgoing edge carrying `data`
// units, each of the k resources sends its own data / k piece to the
// resource(s) running the dependent task (see Input_format.md, @comm).
//
// Independently of the category, a sentinel value < 0 in @times/@cost marks
// a FORBIDDEN assignment of that task to that resource (used e.g. for the
// "specific" dedicated resources of a DT task).
//
// Task repetition from the ETG spec is NOT modeled (open question for the
// instructor).
enum class Category { GT, DT, UT, CDT, CGT };

const char* categoryName(Category c);

// Parse a task token like "GT0", "CDT3", "DT8" or a bare "T0" into
// (category, id). Returns false if the token is malformed.
bool parseTaskToken(const std::string& tok, Category& cat, int& id);

struct Task {
    int id = -1;
    int declaredSucc = 0;
    Category cat = Category::GT;
    std::vector<Edge> successors;

    // A common task may run on several resources at once; the count is
    // decided by the optimizer, not by the file.
    bool isCommon() const { return cat == Category::CDT || cat == Category::CGT; }
};

// @proc row interpretation (3 columns):
//   raw[0] - one-time purchase/activation cost, paid once iff the resource
//            executes at least one task,
//   raw[1] - reserved/unknown (always 0 in the available instances),
//   raw[2] - type flag: 1 = universal (may execute many tasks sequentially),
//            0 = specialized (executes exactly ONE task, per the ETG spec).
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

// Returns true if task t may be assigned to processor p, considering both
// the category filter (GT/DT/UT/CDT/CGT) and sentinel values in the matrices.
bool assignmentAllowed(const ETG& g, int t, int p);

}

#endif