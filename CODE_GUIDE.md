# Code Guide — Constructive Genetic Programming, element by element

> Defense notes. Every key element of the solution: **what it is → why it is designed this
> way → where it lives in the code → the actual code fragment.**

---

## 1. What makes this *constructive* genetic programming

**What:** the individual does not encode a schedule ("task 3 → processor 2"). It encodes a
**decision function per task** — *how* to choose the processor(s) and the channel. The
schedule (phenotype) is **constructed** by walking the tasks in topological order and
applying each task's function to the *current state* of the partial schedule.

**Why:** the functions see runtime context (which resources are already bought, busy,
idle), so one small genotype generalizes to good schedules; classic direct encodings can't
react to context. This is exactly the lecture's model: *"nodes have a function that says
how to get the processing element for each task"*.

**Where:** genotype in `src/schedule.h`, decoder loop in `src/evaluator.cpp`
(`evaluateIndividual`).

```cpp
// src/schedule.h — the genotype: decision functions, not assignments
struct Individual {
    std::vector<PeGene>  peGenes;   // size numTasks, index = taskId
    std::vector<ClsGene> clsGenes;  // size numTasks
    Schedule schedule;              // phenotype (filled by the evaluator)
    double fitness = 1e18;          // lower = better
};
```

```cpp
// src/evaluator.cpp — the constructive decoder (skeleton)
for (int taskId : prep.topo) {                       // topological order of the DAG
    std::vector<int> procs = pickProcs(
        ind.peGenes[taskId], graph, prep, tree, taskId, st);   // apply the task's gene
    ...
    double dataReady = predecessorsReadyTime(        // channels via the task's ClsGene
        graph, prep, st, taskId, procs, ind.clsGenes[taskId]);
    ...                                              // schedule the task, update state
}
```

---

## 2. Genotype shape: the spanning tree

**What:** the DAG is spanned by a tree built **once, deterministically**: every task's tree
parent is its smallest-id predecessor. Every individual shares this exact shape; only the
genes stored in the nodes differ.

**Why:** (a) subtree crossover can never break the structure, because "the same subtree"
means the same node set in both parents; (b) the *same-as-predecessor* gene needs one
well-defined parent, while a DAG node may have many predecessors.

**Where:** `src/spanning_tree.cpp`.

```cpp
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
        tree.parent[t] = best;      // deterministic: smallest-id predecessor
    }

    return tree;
}
```

---

## 3. The gene function set (PE — processor selection)

**What:** 8 heuristics. The first five operate on **allocated** resources (already used in
this schedule), the last three on all legal resources.

| Gene | Meaning (score, min wins) |
|---|---|
| `AllocCheapest` | cheapest among already-used resources |
| `AllocFastest` | fastest among already-used |
| `AllocLFU` | least frequently used (`peUseCount`) |
| `AllocIdle` | idle the longest = smallest `lastFinish` |
| `AllocSamePred` | the spanning-tree parent's processor (locality → free communication) |
| `Cheapest` | globally cheapest (execution cost + purchase cost if not bought yet) |
| `Fastest` | globally fastest |
| `MinTS` | min `time × cost` |

**Why these:** they are the lecture's function set, mapped onto the ETG cost model
("area" → cost).

**Where:** `src/genes.h` (enum), `src/evaluator.cpp` → `pickSingleProc` (application).

```cpp
// src/evaluator.cpp — applying a PE gene to a single (GT/DT/UT) task
int pickSingleProc(PeGene gene, ..., const std::vector<int>& pool)
{
    ...
    for (int p : pool) {
        double score = 0.0;
        switch (gene) {
            case PeGene::AllocCheapest:
            case PeGene::Cheapest:
                score = procExecCost(graph, taskId, p, st);   // + buyCost if unbought
                break;
            case PeGene::AllocFastest:
            case PeGene::Fastest:
                score = static_cast<double>(graph.times[taskId][p]);
                break;
            case PeGene::MinTS:
                score = static_cast<double>(graph.times[taskId][p]) *
                        static_cast<double>(graph.costs[taskId][p]);
                break;
            case PeGene::AllocLFU:
                score = static_cast<double>(st.peUseCount[p]);
                break;
            case PeGene::AllocIdle:
                score = st.lastFinish[p];      // smallest = idle the longest
                break;
            ...
        }
        if (score < bestScore) { bestScore = score; best = p; }
    }
    return best;
}
```

**The "allocated" pool + graceful degradation** — if no already-used resource is legal for
the task, the gene degrades to the global `Cheapest` (consistent with the cost objective):

```cpp
// src/evaluator.cpp — pickProcs
std::vector<int> pool = candidates;                    // allowed ∩ available
if (gene == PeGene::AllocCheapest || gene == PeGene::AllocFastest ||
    gene == PeGene::AllocLFU || gene == PeGene::AllocIdle ||
    gene == PeGene::AllocSamePred)
{
    std::vector<int> used = filterUsed(pool, st);      // keep already-used resources
    if (!used.empty())
        pool = used;
    else
        gene = PeGene::Cheapest;                       // fallback
}
```

**Same-as-predecessor** — takes the tree parent's processor; if it is illegal/unavailable,
falls back to `Cheapest` (team decision — consistent with the cost objective):

```cpp
if (gene == PeGene::AllocSamePred) {
    int p = -1;
    if (pickSameAsPred(graph, prep, tree, taskId, st, p))
        return p;
    return pickSingleProc(PeGene::Cheapest, graph, prep, tree, taskId, st, pool);
}
```

---

## 4. The gene function set (CLS — channel selection)

**What:** 7 heuristics, applied by the **receiver's** gene for every edge with `data > 0`
whose endpoints run on different processors. "Allocated" = channels both endpoints are
already connected to (connection cost already paid).

**Where:** `src/evaluator.cpp` → `pickChannel`.

```cpp
switch (gene) {
    case ClsGene::ClsAllocCheapest:
    case ClsGene::ClsCheapest:
        score = static_cast<double>(ch.connectCost) +
                channelMarginalCost(graph, st, c, sender, receiver);
        break;
    case ClsGene::ClsAllocFastest:
    case ClsGene::ClsHighestBw:
        score = -static_cast<double>(ch.bandwidth);    // max bandwidth wins
        break;
    case ClsGene::ClsAllocLFU:
    case ClsGene::ClsLFU:
        score = static_cast<double>(st.channelUseCount[c]);
        break;
    case ClsGene::ClsAllocIdle:
        score = st.channelLastFinish[c];               // smallest = idle the longest
        break;
    ...
}
```

Transfers on the **same** processor are local — free and instantaneous. Channel connection
cost is paid **once** per (channel, resource) pair (`channelMarginalCost` adds it only for
endpoints not yet connected).

---

## 5. Common tasks (CDT/CGT): subset choice + true parallelism

**What:** for a common task the optimizer chooses **how many and which** resources run it
(`k` is part of the solution, not the input). The evaluator enumerates **all non-empty
subsets** of the allowed & available resources (bitmask loop) and scores each subset with
the active gene. Each chosen resource does a `1/k` share **in parallel**:

```
time(S)  = max_p ( times[t][p] / k )    // the slowest share ends the task
cost(S)  = Σ_p   ( costs[t][p] / k )    // cost is genuinely split
```

**Why max, not sum:** shares run simultaneously — with the old sum formula the pieces
effectively ran one after another and splitting never paid off. With `max`, two identical
resources halve the time at the same execution cost, so parallelism **buys time** under a
tight `Tmax` (the point of CDT/CGT).

**Where:** `src/evaluator.cpp` → `pickCommonProcs`, `scoreCommonSubset`, `commonExecTime`,
and the common branch of `evaluateIndividual`.

```cpp
// subset enumeration + gene-specific scoring
for (int mask = 1; mask < (1 << n); ++mask) {
    std::vector<int> subset;
    for (int i = 0; i < n; ++i)
        if (mask & (1 << i))
            subset.push_back(pool[i]);

    double score = scoreCommonSubset(gene, graph, taskId, subset, st);
    if (score < bestScore) { bestScore = score; result = subset; }
}
```

```cpp
// Parallel 1/k shares: the task ends when the slowest share ends.
double commonExecTime(const etg::ETG& graph, int taskId, const std::vector<int>& procs) {
    int k = static_cast<int>(procs.size());
    double longest = 0.0;
    for (int p : procs) {
        double share = static_cast<double>(graph.times[taskId][p]) / static_cast<double>(k);
        if (share > longest)
            longest = share;
    }
    return longest;
}
```

```cpp
// evaluateIndividual — each processor starts its own share when IT is free;
// a busy resource delays only its own piece
for (int p : procs) {
    double pieceStart = dataReady;
    if (st.freeAt[p] > pieceStart)
        pieceStart = st.freeAt[p];
    double share = static_cast<double>(graph.times[taskId][p])
                   / static_cast<double>(k);
    double pieceFinish = pieceStart + share;
    if (firstStart < 0.0 || pieceStart < firstStart)
        firstStart = pieceStart;
    if (pieceFinish > lastPieceFinish)
        lastPieceFinish = pieceFinish;
    markProcUsed(graph, st, p, pieceFinish);   // freed after ITS OWN piece
}
assign.startTime = firstStart;
assign.finishTime = lastPieceFinish;           // data is complete only now
```

Subset scoring per gene (`scoreCommonSubset`): `Cheapest` → `cost(S)`, `Fastest` →
`time(S)`, `MinTS` → `time·cost`, `LFU` → sum of use counts over `S`, `Idle` → the most
idle member (`min lastFinish`), `SamePred` → the parent's processor as a `k=1`
representative.

**Communication out of a common task:** the result exists in `k` pieces; sending starts
only after the whole task finishes (the evaluator uses the predecessor's `finishTime` =
max share), each resource sends its own `data/k` piece, and the successor's start waits
for the **maximum** over all piece arrivals — i.e. for the complete data set.

**Complexity note:** up to `2^|allowed|` subsets per common task — fine for project-sized
instances (documented; a greedy fallback above a threshold was considered and deemed
unnecessary).

---

## 6. Fitness: cost under a hard time constraint

**What:** minimize cost; the time limit is enforced with a λ-penalty.

**Why a penalty (not rejection):** infeasible individuals still carry useful genetic
material; with a large λ any feasible schedule outranks any infeasible one, so the
selection pressure is exactly "cost, subject to Tmax". Rank selection makes the exact λ
magnitude irrelevant (only the ordering matters).

**Where:** end of `evaluateIndividual` (`src/evaluator.cpp`).

```cpp
if (params.tmax > 0 &&
    ind.schedule.makespan > static_cast<double>(params.tmax))
{
    ind.fitness = ind.schedule.totalCost +
        params.lambda * (ind.schedule.makespan - static_cast<double>(params.tmax));
} else {
    ind.fitness = ind.schedule.totalCost;
}
```

Total cost = Σ execution costs + Σ purchase costs of used resources (paid once, on first
use — see `procExecCost` / `commonExecCost`) + Σ channel connection costs. An assignment
with no available processor gets `params.penalty` (1e12) — effectively eliminated.

---

## 7. Weighted gene initialization

**What:** initial genes (and mutation replacements) are drawn with fixed weights, not
uniformly. The two-level distribution from the design (60% for the "allocated" group,
split internally) is flattened into per-gene weights.

**Where:** `src/genes.cpp`.

```cpp
constexpr std::array<double, 8> kPeWeights = {
    12.0, 12.0, 12.0, 12.0, 12.0,   // Alloc* genes: 60% * 20% each
    10.0,                            // Cheapest
    10.0,                            // Fastest
    20.0,                            // MinTS
};
static_assert(kPeWeights.size() == static_cast<std::size_t>(PeGene::COUNT), ...);

PeGene randomPeGene(std::mt19937& rng) {
    static std::discrete_distribution<int> dist(kPeWeights.begin(), kPeWeights.end());
    return static_cast<PeGene>(dist(rng));
}
```

(CLS analogously: `12, 12, 18, 18, 10, 10, 20` — the allocated LFU/Idle channel genes got
30% of the 60% group.)

---

## 8. Population size derived from the instance

**What:** `POP = round(α · n · τ)` where `n` = number of tasks and `τ` = number of distinct
resource **types** present in `@proc` (universal / specialized → 1 or 2).

**Why:** bigger / more heterogeneous instances get bigger populations automatically —
consistent with "the program must read any ETG". `τ` is isolated in one small function so
its definition can be changed without touching the algorithm.

**Where:** `src/ga.cpp`.

```cpp
int countPeTypes(const etg::ETG& graph) {
    bool universal = false;
    bool specialized = false;
    for (const etg::Processor& p : graph.procs) {
        if (p.isUniversal()) universal = true;
        else                 specialized = true;
    }
    int types = (universal ? 1 : 0) + (specialized ? 1 : 0);
    return types > 0 ? types : 1;
}
...
const int popSize = std::max(2, static_cast<int>(
    std::lround(gaParams.alpha * graph.numTasks * tau)));
```

---

## 9. Selection: linear rank selection

**What:** the population is sorted by fitness (rank 0 = best); an individual's selection
probability depends only on its **rank**, decreasing linearly:

```
p(i) = (1/POP) · ( sp − (2·sp − 2) · i/(POP−1) ),   sp ∈ [1, 2]
```

`sp` = selection pressure = expected number of offspring of the best individual
(`sp = 1` → uniform, `sp = 2` → the worst gets probability 0). The probabilities sum to 1.

**Why rank, not roulette:** our fitness contains huge λ-penalties; fitness-proportional
selection would be dominated by those magnitudes. Rank selection uses only the *ordering*,
so penalties work as intended and no fitness scaling is needed.

**Where:** `src/ga.cpp`.

```cpp
std::vector<double> linearRankProbs(int popSize, double sp) {
    std::vector<double> probs(popSize, 1.0);
    if (popSize <= 1)
        return probs;
    for (int i = 0; i < popSize; ++i) {
        probs[i] = (sp - (2.0 * sp - 2.0) * static_cast<double>(i)
                    / static_cast<double>(popSize - 1))
                   / static_cast<double>(popSize);
    }
    return probs;
}
...
const std::vector<double> probs = linearRankProbs(popSize, gaParams.rankPressure);
std::discrete_distribution<int> rankDist(probs.begin(), probs.end());
// rankDist(rng) returns a RANK; pop is kept sorted, so pop[rankDist(rng)] is the pick
```

The same selection is used for **all three** channels: crossover parents, mutation
candidates and clones.

---

## 10. Crossover: subtree crossover (2 parents → 1 child)

**What:** draw a random node `r`; the child is a copy of parent A with the genes of the
**entire subtree rooted at `r`** (in the shared spanning tree) overwritten from parent B.

**Why:** this is the lecture's "copy the same part of the tree between genotypes". Because
every individual shares one tree shape, the node set of the subtree is identical in both
parents — the operation is always structurally safe, and it transfers a **coherent block**
of related decisions (a branch of the process) instead of independent random genes.

**Where:** `src/ga.cpp`.

```cpp
bool inSubtree(const SpanningTree& tree, int node, int root) {
    for (int cur = node; cur >= 0; cur = tree.parent[cur])
        if (cur == root)
            return true;
    return false;
}

Individual subtreeCrossover(const Individual& a, const Individual& b,
                            const SpanningTree& tree, std::mt19937& rng)
{
    Individual child;
    child.peGenes = a.peGenes;
    child.clsGenes = a.clsGenes;
    ...
    std::uniform_int_distribution<int> nodeDist(0, n - 1);
    int root = nodeDist(rng);

    for (int t = 0; t < n; ++t) {
        if (inSubtree(tree, t, root)) {          // t is in the subtree of root
            child.peGenes[t] = b.peGenes[t];     // take the whole block from B
            child.clsGenes[t] = b.clsGenes[t];
        }
    }
    return child;
}
```

---

## 11. Mutation: forced gene replacement in one node

**What:** pick a random node, flip a coin (PE vs CLS gene), replace that gene with a
different weighted-random one. The redraw loop guarantees the gene actually changes.

**Why forced (not per-probability):** in this scheme the **number of mutants is fixed by
`γ`** (`nMut = γ·POP`), so the operator itself must always do something — probability
`Pm`-style no-ops would silently shrink the mutation fraction.

**Where:** `src/ga.cpp`.

```cpp
void mutateForce(Individual& ind, std::mt19937& rng) {
    ...
    int t = taskDist(rng);
    if (coin(rng) == 0) {
        PeGene g = ind.peGenes[t];
        for (int i = 0; i < 16 && g == ind.peGenes[t]; ++i)
            g = randomPeGene(rng);               // redraw until different
        ind.peGenes[t] = g;
    } else {
        ClsGene g = ind.clsGenes[t];
        for (int i = 0; i < 16 && g == ind.clsGenes[t]; ++i)
            g = randomClsGene(rng);
        ind.clsGenes[t] = g;
    }
}
```

---

## 12. Generation assembly: disjoint fractions β/γ/δ + elitism

**What:** each new generation consists of exactly `POP` individuals produced by **three
disjoint channels**: `nCross = round(β·POP)` crossover children, `nMut = round(γ·POP)`
mutants, `nClone = POP − nCross − nMut` clones. The first clone is always the current
best (elitism), so the best solution can never be lost.

**Why `nClone` as the remainder:** three independent `round()`s may not sum to `POP`; the
cloning fraction absorbs the rounding error, and a small loop guarantees `nClone ≥ 1`.
`β + γ + δ = 1` is validated at startup.

**Where:** `src/ga.cpp` → `runGa`.

```cpp
int nCross = static_cast<int>(std::lround(gaParams.beta * popSize));
int nMut = static_cast<int>(std::lround(gaParams.gamma * popSize));
int nClone = popSize - nCross - nMut;
while (nClone < 1) {                       // keep at least the elite clone
    if (nCross >= nMut && nCross > 0) --nCross;
    else if (nMut > 0)                --nMut;
    ++nClone;
}
...
next.push_back(pop[0]);                    // elitism: best always survives
for (int i = 1; i < nClone; ++i)
    next.push_back(pop[rankDist(rng)]);    // remaining clones: rank selection

for (int i = 0; i < nCross; ++i) {         // crossover channel
    const Individual& a = pop[rankDist(rng)];
    const Individual& b = pop[rankDist(rng)];
    next.push_back(subtreeCrossover(a, b, tree, rng));
}

for (int i = 0; i < nMut; ++i) {           // mutation channel (independent source!)
    Individual m = pop[rankDist(rng)];
    mutateForce(m, rng);
    next.push_back(m);
}
```

Note: mutation is **not** applied on top of crossover children — it is an independent
source of individuals. That is why the fractions can sum to 1.

---

## 13. Dynamic stop condition

**What:** the loop ends when the best fitness has not improved (by more than `eps`) for
`noImproveLimit` consecutive generations; `maxGenerations` is only a safety cap.

**Why:** run time adapts to the instance — easy instances stop quickly, hard ones get more
generations, without hand-tuning a fixed generation count.

**Where:** `src/ga.cpp` → `runGa` (loop tail).

```cpp
if (pop[0].fitness < bestFit - eps) {
    result.best = pop[0];
    bestFit = pop[0].fitness;
    noImprove = 0;
} else {
    noImprove += 1;
}

if (noImprove >= gaParams.noImproveLimit) {
    result.stoppedByNoImprove = true;
    break;
}
```

---

## 14. Parameter validation & determinism

**Where:** `src/ga.cpp` → `validateGaParams` (called at the top of `runGa`; throws
`std::invalid_argument`, which `main` reports and exits with code 1).

```cpp
if (std::abs(p.beta + p.gamma + p.delta - 1.0) > 1e-9)
    throw std::invalid_argument("GaParams: beta + gamma + delta must equal 1");
if (p.rankPressure < 1.0 || p.rankPressure > 2.0)
    throw std::invalid_argument("GaParams: rank pressure must be in [1,2]");
```

Everything is driven by one `std::mt19937 rng(gaParams.seed)` — identical input +
parameters + seed ⇒ identical result (covered by tests).

---

## 15. Likely defense questions — quick answers

* **Why is this GP and not a plain GA?** We evolve a *set of decision functions* and
  *construct* the solution by executing them; a plain GA would encode the assignment
  directly. Mechanically the chromosome is fixed-length, but the semantics (functions in
  tree nodes + constructive decoder) is the lecture's constructive GP.
* **Why is the spanning tree fixed?** All decision variability lives in the node genes;
  a fixed shared shape makes subtree crossover always safe and gives *same-as-predecessor*
  a unique parent. Evolving tree shapes would add repair machinery with no clear gain.
* **Why rank selection instead of roulette?** Fitness contains huge λ-penalties; rank uses
  only ordering, so penalties don't distort selection.
* **Why `max` (not sum) for common task time?** Shares run **simultaneously** — the task
  ends when the slowest share ends. Sum would model sequential execution and `k > 1` would
  never pay off (the mean of shares is never below the best single resource).
* **When does splitting a common task help?** Under a tight `Tmax`: it divides time at
  equal execution cost (plus extra purchase costs). Pure cost minimization still prefers
  `k = 1`. Demo: `input_parallel.txt` (see README).
* **How is Tmax enforced?** λ-penalty on the excess; with rank selection any feasible
  schedule outranks any infeasible one.
* **What is the complexity hotspot?** Subset enumeration for common tasks, `O(2^|allowed|)`
  per task per evaluation — acceptable for project instances, documented.
* **What is deliberately not modeled?** Task repetition (open question for the instructor),
  channel contention (transfers don't queue), integer division of `data/k`.
