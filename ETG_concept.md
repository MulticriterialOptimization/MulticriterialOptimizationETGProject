# Extended Task Graph (ETG)

## What an ETG is

An Extended Task Graph (ETG) is a directed acyclic graph used to model and
solve real-life optimization problems. It extends the classical task graph
with richer representations of tasks and their execution constraints.

In a traditional task graph, tasks are nodes and dependencies are edges. The
graph is acyclic and focuses mainly on execution order and data flow. All
tasks are treated uniformly, which limits its applicability in complex
real-world scenarios.

The ETG overcomes this by distinguishing different *types* of tasks based on
how they can be executed and what resources they require. The focus shifts
from "only dependencies" to actual execution conditions.

## Task categories

In this project the category of a task is written directly as the prefix of
its id in the `@tasks` section (e.g. `GT0`, `UT2`, `CDT3`, `CGT4`, `DT8`).
There is no separate `@type` section.

| Category | Prefix | Meaning |
|---|---|---|
| Dedicated Task        | `DT`  | can be executed only by specific specialized resources |
| General Task          | `GT`  | can be executed by one resource of any type |
| Universal Task        | `UT`  | can be executed only by one universal resource |
| Common Dedicated Task | `CDT` | can be executed by several specialized resources simultaneously |
| Common General Task   | `CGT` | can be executed by several resources of any type simultaneously |

A `< 0` sentinel in `@times`/`@cost` forbids a specific task/resource pair and
is used, for example, to narrow a DT task down to its "specific" resources.

## Resources

Resources (processors) are divided into two kinds, encoded by the type flag in
the `@proc` section:

- **Universal** (flag `1`): flexible, can execute many tasks sequentially
  (one at a time). Tend to be slower/cheaper per task.
- **Specialized** (flag `0`): execute exactly one task in the whole schedule.
  Tend to be fast but expensive.

Each resource also has a one-time purchase/activation cost, paid once if and
only if the resource executes at least one task.

## Cost and time model

For an ordinary task (`GT`, `DT`, `UT`) assigned to one resource `p`, the
execution time and cost are simply the matrix entries `times[t][p]` and
`costs[t][p]`.

### Common tasks (CDT / CGT) — the important part

A common task is **not** assigned a fixed number of resources by the input
file. How many resources `k` execute it is decided by the optimizer (the
genetic algorithm), and there is **no upper limit** on `k` (beyond the number
of resources the category and sentinels allow).

When a common task `t` runs on a set `S` of `k` resources, each chosen
resource performs a `1/k` share of the work and the shares run **in
parallel** (each resource starts its share as soon as it is free and the
input data is ready — a busy resource delays only its own piece):

- **total time** = max over `p ∈ S` of `times[t][p] / k`
  (the task ends when the slowest share ends)
- **total cost** = sum over `p ∈ S` of `costs[t][p] / k`

With `k` identical resources the task runs about `k`× faster at the same
total cost, so splitting a common task genuinely pays off time-wise.
For `k = 1` both formulas reduce to the ordinary case.

### Communication out of a common task

A common task produces its result in `k` pieces — one per participating
resource — and the result is complete only when the WHOLE task finishes
(all shares done). Only then, for every outgoing edge that carries `data`
units, **each of the `k` resources sends its own `data / k` piece** to the
resource(s) running the dependent task; the dependent task may start only
once it has received ALL pieces.

Transferring a `data / k` piece over a channel of bandwidth `b` takes
`ceil((data / k) / b)` time units. The per-(channel, resource) connection cost
is paid for every resource that actually transfers a piece. If a piece stays
on a resource that also runs the dependent task, that piece is local (free and
instantaneous).

## Project goal

Use the ETG as a specification of an aircraft
production process and optimize the allocation of tasks to resources with a
constructive genetic-programming approach. The algorithm optimizes **cost**
under a given **time constraint** (makespan limit `Tmax`).

`Tmax` is a parameter of the optimizer (program argument), not part of the ETG
file. Task repetition from the general ETG specification is currently not
modeled (open question for the instructor). The program must read **any** ETG
instance — the production process is not assumed to be specified in only one
way.
