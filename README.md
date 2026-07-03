# Constructive Genetic Programming for the Extended Task Graph (ETG)

## Problem

An **Extended Task Graph (ETG)** is given as the specification of an aircraft production
process. It is a directed acyclic graph: nodes are tasks, edges are dependencies (optionally
carrying a volume of data to transfer). Unlike a classical task graph, tasks are divided into
categories that define **how** they may be executed:

| Category | Prefix | May be executed by |
|---|---|---|
| Dedicated Task | `DT` | only **specific specialized** resources |
| General Task | `GT` | **one** resource of any type |
| Universal Task | `UT` | only **one universal** resource |
| Common Dedicated Task | `CDT` | **multiple specialized** resources working **simultaneously** |
| Common General Task | `CGT` | **multiple** resources of any type working **simultaneously** |

Resources come in two kinds: **universal** (flexible, may execute many tasks sequentially,
one at a time) and **specialized** (fast but expensive, executes **exactly one** task in the
whole schedule). Every resource has a one-time purchase cost, and every (task, resource) pair
has an execution time and cost given by matrices. Data transferred between tasks running on
different resources travels through **communication channels** (connection cost per
(channel, resource) pair, transfer time = `ceil(data / bandwidth)`).

**The goal:** using a **constructive genetic programming** approach, find an allocation of
tasks to resources (and channels) that **minimizes the total cost** while keeping the
schedule **makespan ≤ Tmax** (a hard time constraint given as a program argument).

**The program must be able to read any extended task graph** - no assumptions are made about
the shape of the production process.

`Gene – a decision heuristic stored in one node of the task graph's spanning tree: HOW to
pick the processor(s) for that task (PeGene) and HOW to pick the communication channel when
the task receives data (ClsGene).`

`Genotype – the spanning tree of the task graph (same fixed shape for every individual) with
one (PeGene, ClsGene) pair per node.`

This means an individual does **not** encode the schedule directly - it encodes a **set of
decision functions**, and the schedule (phenotype) is **constructed** by applying them. That
is the essence of *constructive* genetic programming.

### Spanning tree

* **Nodes are tasks**; the whole tree represents the **dependencies tasks have on each other**
  (it is a spanning tree of the task graph, so it keeps every task but reduces the DAG so that
  each task has exactly one parent).
* **Edges are a parent -> child relationship**, where the parent is a task's predecessor.
* It is built **once, deterministically** (`src/spanning_tree.cpp`): `parent[t]` = the
  predecessor of `t` with the smallest id; a task with no predecessors is a root (`parent = -1`).
* The tree shape is the **same for every individual** - only the genes stored per task differ.

### How the tree and the genes work together

* The tree (`parent[]`) and the genes (`peGenes[t]`, `clsGenes[t]`) are **separate arrays
  joined by task id**: task `t` is a tree node, its rule is `peGenes[t]` / `clsGenes[t]`.
* The tree is used in exactly two places: 
  * the **AllocSamePred** gene (reuse the tree parent's processor) - `pickSameAsPred()` src/evaluator.cpp
  * and **subtree crossover** (swap the genes of a whole subtree between
  two individuals).

### Phenotype

The **phenotype** is the finished schedule (task -> resources, start/finish times, cost); it is
built in `evaluateIndividual` (`src/evaluator.cpp`) by walking the tasks in topological order
and applying each task's genes to the state built so far.

## My solution

0. **Input**: a plain-text ETG file (sections `@tasks`, `@proc`, `@times`, `@cost`,
   optional `@comm`), parsed by `src/etg.cpp`. `Tmax` and all GA parameters are **CLI
   arguments**, not part of the instance file.
1. **Validation** (`validateETG`): the graph must be a DAG, matrices must match declared
   dimensions, every task must have at least one allowed resource (category filter +
   `< 0` sentinels in the matrices), channel flags must match the resource count. All
   problems are collected and reported at once.
2. **Preparation** (`src/etg_prep.cpp`): computes for every task the list of **allowed
   resources**, the **predecessor lists** and a **topological order** of the whole DAG.
   * Predecessor list used when checking when all predecessor are done and to build spanning tree.
   * Topological order is used in `src/evaluator.cpp` to go through task in correct order, so all of the predecessor are calculated before current task.
3. **Spanning tree** (`src/spanning_tree.cpp`): built **deterministically, once per
   instance** - `parent[t]` = the predecessor of `t` with the smallest id (roots have
   `parent = -1`). The tree shape is **identical for every individual**, so crossover can
   never break the genotype structure. The tree is used by the *subtree crossover* and by
   the *"same as predecessor - AllocSamePred"* gene.
4. **Genotype** (`src/genes.h`, `src/schedule.h`): two arrays indexed by task id -
   `peGenes[t]` (8 processor-selection heuristics) and `clsGenes[t]` (7 channel-selection
   heuristics). Initial genes are drawn with **fixed, weighted probabilities**
   (`src/genes.cpp`), e.g. the "allocated" gene group has a combined 60% weight.
   * `randomPeGene/randomClsGene` - are used in 2 places. (choosing genes based on the weights)
     * Creating initial population.
     * Mutation - used to mute node.
5. **Evaluator = constructive decoder** (`src/evaluator.cpp`): walks the tasks in
   topological order and *builds* the schedule step by step. For every task it applies the
   task's `PeGene` to the current state (which resources are already bought, busy, idle…),
   * for **CDT/CGT** it evaluates **every subset** of the allowed resources and picks the best
   one (best subset of allowed resources which worrs with each other) according to the active gene. Common tasks run their `1/k` shares **in parallel**:
   each chosen resource starts its share as soon as it is free, and the task finishes when
   the **slowest share** finishes (`time = max(times[t][p]/k)`, `cost = Σ costs[t][p]/k`).
   Data produced by a common task is sent (in `data/k` pieces) only after the **whole task**
   completes, and a successor may start only once it has **all** pieces. Channels are chosen
   by the **receiver's** `ClsGene`; transfers between tasks on the same resource are local (so no communication cost)
6. **Fitness** (end of `evaluateIndividual`): *lower is better*.
   `fitness = totalCost` if `makespan ≤ Tmax`, otherwise
   `fitness = totalCost + λ·(makespan − Tmax)`. 
   * Total cost = execution costs + one-time
   purchase costs of used resources + channel connection costs. An unschedulable
   assignment (no available resource) gets a huge penalty (`double penalty = 1e12;`).
   * λ - is a pentalty weight.
7. **Genetic loop** (`src/ga.cpp`, `runGa`): the population size is derived from the
   instance - `POP = round(α · numTasks · numPeTypes)`, where `numPeTypes` = number of
   resource types present (1 or 2), computed by `countPeTypes()`. **Parameters:**
   * `α` (alpha) - scales the population with the instance size.
   * `β` (beta), `γ` (gamma), `δ` (delta) - the three generation **fractions**, must satisfy
     `β + γ + δ = 1` (checked by `validateGaParams()`).
   * `sp` (rank pressure, `--rank-pressure`, `∈ [1,2]`) - how strongly better-ranked
     individuals are preferred (1 = uniform, 2 = strongest); used by `linearRankProbs()`.

   Every generation is assembled from three **disjoint fractions** (`β, γ, δ`):
   * **cloning** (`δ·POP`): the best individual is always cloned (elitism), the remaining
     clones are picked by rank selection.
   * **crossover** (`β·POP`): two parents picked by **linear rank selection**, one child per
     operation via **subtree crossover** - `subtreeCrossover()` in `src/ga.cpp` (a random
     node is drawn, the genes of its whole subtree are copied from parent B into a copy of A).
   * **mutation** (`γ·POP`): one rank-selected individual is cloned and **exactly one gene in
     a random node is replaced** - `mutateForce()` in `src/ga.cpp` (the mutant count is fixed
     by `γ`, so the operator never no-ops).

   **Selection**: the population is sorted (index = rank, best first), `linearRankProbs()`
   turns ranks into weights, and `std::discrete_distribution` draws a rank from those weights -
   a weighted lottery where better ranks are picked more often but weaker ones still get a
   chance. `pop[drawnRank]` is the selected individual (same draw feeds crossover, mutation
   and cloning). The loop stops **dynamically** when the best fitness has not improved for
   `--no-improve` consecutive generations (with `--max-gen` as a hard safety limit).
8. **Output**: the best schedule (resources per task, start/finish times, per-task cost),
   total cost, makespan, fitness and the number of generations run.

### Important Info

* The program reads **any** valid ETG: any number of tasks/resources/channels, sections in
  any order, categories encoded in the task-id prefix (a bare `T0` is accepted as `GT` for
  legacy files), `@comm` optional.
* **Every resource is reusable and runs its tasks sequentially** (one at a time - a busy
  resource delays the next task assigned to it, tracked by `freeAt`). The two kinds differ
  only in **cost/speed** and in **which tasks may use them**: specialized resources are
  pricier/faster and are the **only** ones allowed to run `DT` / `CDT` tasks (universal
  resources cannot); `GT` tasks may run on either kind. There is **no** one-time-use limit.
* **Communication**: the channel-selection rule belongs to the **receiver** (`clsGenes[t]`),
  applied per incoming data edge; both the **sender's and the receiver's** processor pay the
  channel `connectCost` when they are not yet connected to it (same processor = local, free).
* For common tasks (CDT/CGT) the number of resources `k` is **chosen by the optimizer** -
  it is not part of the input. Shares run in parallel, so splitting a task **buys time at
  the same execution cost** (only extra purchase costs are added). With a tight `Tmax` the
  GA genuinely uses `k > 1`; without a time limit a single cheapest resource usually wins.
* `--tmax` is a hard constraint enforced through a λ-penalty: any schedule within `Tmax`
  beats any schedule that exceeds it (for a sensibly large `λ`, default 1000). If `--tmax`
  is not given, the program simply minimizes cost.
* `beta + gamma + delta` **must equal 1** (validated at start, the program exits with an
  error message otherwise). `gamma, delta ∈ (0,1)`, `alpha > 0`, `rank pressure ∈ [1,2]`.
* Runs are **deterministic**: the same input + parameters + seed always produce the same
  result.

## Project structure

```
src/main.cpp             # entry point: CLI, parsing, validation, runs the GA
src/etg.h / etg.cpp      # ETG data model, text parser, validation
src/etg_prep.h / .cpp    # allowed resources, predecessors, topological order
src/spanning_tree.*      # deterministic spanning tree of the DAG (genotype shape)
src/genes.h / genes.cpp  # PeGene / ClsGene enums + weighted random gene drawing
src/schedule.h           # Individual (genotype) and Schedule (phenotype) types
src/evaluator.h / .cpp   # constructive decoder: genes -> schedule -> fitness
src/ga.h / ga.cpp        # the genetic loop: rank selection, subtree crossover,
                         #   forced mutation, cloning/elitism, dynamic stop
input.txt                # sample ETG instance (10 tasks, 4 resources, 1 channel)
input_parallel.txt       # demo instance where a common task must be split (k = 2)
```

## Requirements

* **C++20**
* **CMake 3.23+**

## How to build & run

```bash
Use Visual Studio. Open project solution and Run Release version with configurable parameters.
cmake -S . -B build
cmake --build build --config Release

# run on the sample instance with a time limit
build\Release\etg_solver.exe input.txt --tmax 40 --alpha 5 --no-improve 50

# example: a tight Tmax forces a common task onto two resources in parallel
build\Release\etg_solver.exe input_parallel.txt --tmax 30 --alpha 10

# no time limit = pure cost minimization
build\Release\etg_solver.exe input.txt

```

### CLI parameters

| Flag | Meaning | Default |
|---|---|---|
| `--tmax N` | hard makespan limit (0 = no limit) | not set |
| `--lambda N` | penalty weight for exceeding `Tmax` | 1000 |
| `--seed N` | random seed (reproducible runs) | 42 |
| `--alpha X` | population scale: `POP = round(alpha·n·tau)` (`n` = tasks, `tau` = number of resource types present, 1 or 2) | 1.0 |
| `--beta X` | fraction of the generation created by **crossover** | 0.6 |
| `--gamma X` | fraction created by **mutation** | 0.3 |
| `--delta X` | fraction created by **cloning** (includes the elite) | 0.1 |
| `--rank-pressure X` | selection pressure `sp ∈ [1,2]` of linear rank selection (1 = uniform, 2 = strongest) | 1.5 |
| `--no-improve N` | dynamic stop: quit after N generations without improvement | 20 |
| `--max-gen N` | hard cap on generations | 1000 |

## Input format

Full specification in `Input_format.md`. Short version - a plain-text file with sections:

```
@tasks 10
GT0 2 1(0) 2(0)
GT1 2 3(0) 5(0)
UT2 3 9(0) 4(0) 6(0)
CDT3 2 7(0) 9(0)
CGT4 1 8(0)
GT5 1 9(0)
CGT6 0
CGT7 0
DT8 0
CGT9 0
@proc 4
100 0 1
200 0 1
500 0 0
300 0 0
@times
30 10 3 4
50 20 6 5
20 10 -1 -1
-1 -1 1 2
30 15 4 10
50 30 5 5
40 15 10 12
30 15 5 8
-1 -1 2 4
10 5 3 4
@cost
3 2 50 10
5 4 80 20
3 3 -1 -1
-1 -1 20 5
3 2 70 30
5 3 80 15
3 2 70 15
3 2 50 18
-1 -1 30 10
3 1 40 12
@comm 1
CHAN0 15 7 1 1 1 1
```

* `@tasks N` - one line per task. the category is
  the id prefix (`GT`/`DT`/`UT`/`CDT`/`CGT`); successors are referenced by numeric id.
* `@proc M` - one line per resource: `purchaseCost reserved typeFlag`
  (`1` = universal, `0` = specialized).
* `@times` / `@cost` - `N × M` matrices; `< 0` marks a **forbidden** (task, resource) pair.
* `@comm C` - channels: connection cost is paid once per (channel, resource) pair that
  actually transfers data; transferring `d` units takes `ceil(d / bandwidth)`.
* **`Tmax` is NOT part of the file** - it is a program argument.
