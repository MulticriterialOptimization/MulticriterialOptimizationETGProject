# ETG Input File Format

This document describes the text format of an Extended Task Graph (ETG)
instance and how it maps to the data model in `etg.h`.

## General rules

- Plain text, ASCII/UTF-8; both LF and CRLF line endings are accepted.
- The file consists of sections. A section starts with a line beginning
  with `@` (e.g. `@tasks 20`).
- Empty lines are ignored. Unknown sections are skipped.
- Sections may appear in any order, with one practical exception:
  `@tasks` must appear before `@times` and `@cost`, because the task
  count is needed to read the matrices.
- Mandatory sections: `@tasks`, `@proc`, `@times`, `@cost`.
  Optional section: `@comm`.
- Task ids are `0 .. numTasks-1`, resource (processor) ids are
  `0 .. numProcs-1`. Resource ids are implicit: the k-th row of `@proc`
  is resource `Pk`. The task **category is encoded in the id prefix**
  (see `@tasks` below); there is no separate `@type` section.

## `@tasks` — the task graph (mandatory)

```
@tasks N
<CAT><id> <declaredSuccessors> [succ1(data1)] [succ2(data2)] ...
```

- `N` — number of tasks; exactly `N` task lines follow.
- `<CAT><id>` — task identifier: a **category prefix** followed by an
  integer id. The prefix is one of `GT`, `DT`, `UT`, `CDT`, `CGT`
  (see the category table below). A bare `T<id>` is also accepted and
  treated as `GT`, so older files still parse. Examples: `GT0`, `UT2`,
  `CDT3`, `CGT4`, `DT8`.
- `<declaredSuccessors>` — declared number of successors. Used as a
  consistency check only: a mismatch with the actual edge list produces
  a WARNING, not an error (the edge list is the source of truth).
- `succ(data)` — a directed edge to task `<succ>` carrying `data`
  units of data. `data = 0` means a pure precedence constraint with no
  communication. A successor without parentheses means `data = 0`.
  NOTE: successors are referenced by **bare numeric id**, not by the
  category-prefixed token.

Maps to: `struct Task { id, declaredSucc, cat, successors }`,
where each edge is `struct Edge { to, data }`. The category is parsed
from the prefix by `parseTaskToken`.

The graph must be a DAG (no cycles, no self-loops) — checked by
`validateETG`.

Example:

```
@tasks 3
GT0 2 1(10) 2(0)
UT1 1 2(5)
CGT2 0
```

T0 (general task) has two successors: T1 (10 units of data) and T2
(precedence only). T1 (universal task) sends 5 units of data to T2.
T2 (common general task) is the exit task.

## `@proc` — resources (mandatory)

```
@proc M
cost reserved typeFlag
```

One row per resource, `M` rows. Columns:

| Column | Meaning |
|---|---|
| `cost` | one-time purchase/activation cost, paid once iff the resource executes at least one task |
| `reserved` | unknown/reserved; always 0 in known instances (open question for the instructor) |
| `typeFlag` | `1` = UNIVERSAL: may execute many tasks (sequentially, one at a time); `0` = SPECIALIZED: executes exactly ONE task in the whole schedule |

Maps to: `struct Processor { id, raw }` with helpers `cost()`,
`typeFlag()`, `isUniversal()`.

Example:

```
@proc 3
40 0 1
25 0 1
0 0 0
```

P0 and P1 are universal (purchase costs 40 and 25). P2 is specialized:
purchase cost 0 (its real price is expressed per task in `@cost`) and
it can execute at most one task.

## `@times` and `@cost` — execution matrices (mandatory)

```
@times
t[0][0]   t[0][1]   ... t[0][M-1]
...
t[N-1][0] ...           t[N-1][M-1]
```

- Dimensions: exactly `N` rows × `M` columns (validated).
- `times[t][p]` = execution time of task `Tt` on resource `Pp`;
  `costs[t][p]` = execution cost (does NOT include the resource
  purchase cost — that one lives in `@proc`).
- **Sentinel:** a value `< 0` in either matrix marks a FORBIDDEN
  assignment of that task to that resource. This is how task/resource
  restrictions are expressed (e.g. the "specific" dedicated resources
  of a DT task).

Maps to: `ETG::times`, `ETG::costs` (`vector<vector<int>>`).

Example (3 tasks × 3 resources, T1 forbidden on P0):

```
@times
5 9 4
-1 10 3
4 7 2
@cost
10 6 50
-1 7 20
8 5 40
```

## Task categories (encoded in the `@tasks` id prefix)

The category of a task is given by the letter prefix of its id in the
`@tasks` section. There is **no `@type` section**. A bare `T<id>`
(no letters) defaults to `GT`, so legacy files remain valid.

| Category | Prefix | Resources allowed | How many resources |
|---|---|---|---|
| General task            | `GT`  | one resource of any type | 1 |
| Dedicated task          | `DT`  | one SPECIALIZED resource (sentinels may narrow it to specific ones) | 1 |
| Universal task          | `UT`  | one UNIVERSAL resource | 1 |
| Common dedicated task   | `CDT` | SPECIALIZED resources working simultaneously | **chosen by the optimizer (≥1, no upper limit)** |
| Common general task     | `CGT` | resources of any type working simultaneously | **chosen by the optimizer (≥1, no upper limit)** |

The effective set of resources allowed for a task is the intersection of
the category filter (above) and the matrix sentinels (a `< 0` entry in
`@times`/`@cost` forbids that task/resource pair). Every task must have
at least one allowed resource, otherwise the file is rejected.

### Common tasks (CDT / CGT) — cost and time model

Common tasks are NOT given a fixed number of resources in the file. How
many resources `k` execute a common task is part of the **solution**
produced by the genetic algorithm, not part of the instance. There is no
upper limit on `k` (beyond the number of allowed resources).

When a common task `t` runs on a set `S` of `k` resources (`k = |S|`),
each chosen resource does a `1/k` share of the work and the shares run
**in parallel**:

- **total time** = max over `p ∈ S` of `times[t][p] / k`
  (each resource starts its share as soon as it is free and the input
  data is ready — a busy resource delays only its own piece; the task
  ends when the SLOWEST share ends)
- **total cost** = sum over `p ∈ S` of `costs[t][p] / k`

The resources in `S` may differ (each has its own row entry). With `k`
identical resources the task runs about `k`× faster at the same total
cost, so splitting a common task genuinely pays off time-wise.
For a non-common task `k = 1` and both formulas collapse to the plain
matrix entry `times[t][p]` / `costs[t][p]`.

### Communication out of a common task

Because a common task produces its result in `k` pieces (one per
participating resource), each outgoing edge carrying `data` units is also
split: **each of the `k` resources sends its own `data / k` piece** to
the resource(s) running the dependent task. Sending starts only after
the WHOLE task finishes (all shares done), and the dependent task may
start only once it has received ALL pieces. A `data / k` piece sent over
a channel of bandwidth `b` takes `ceil((data / k) / b)` time units, and
the per-(channel, resource) connection cost is paid for each resource
that actually transfers a piece (see `@comm` below). Pieces that stay on
a resource already running the dependent task are local (free,
instantaneous).

Example:

```
@tasks 5
GT0  2 1(0) 2(0)
CGT3 1 4(30)
...
```

If `CGT3` runs on 3 resources, each does `1/3` of the time and cost, and
each sends `30 / 3 = 10` data units to whatever resource executes T4.
## `@comm` — communication channels (optional)

```
@comm C
<name> <connectCost> <bandwidth> f[0] f[1] ... f[M-1]
```

- `C` — number of channels; one line per channel.
- `name` — text label of the channel.
- `connectCost` — one-time cost of connecting ONE resource to this
  channel, paid once per (channel, resource) pair that actually
  transfers data.
- `bandwidth` — transfer of `data` units takes `ceil(data / bandwidth)`
  time units.
- `f[i] ∈ {0,1}` — whether resource `Pi` can use this channel
  (exactly `M` flags, validated).

Communication semantics:

- If the producer and the consumer share a resource, the transfer is
  local: free and instantaneous.
- Otherwise (and `data > 0`), the data must travel through a channel
  that both the sending and the receiving resource can connect to.
  If no such channel exists, the schedule is invalid.

Maps to: `struct CommChannel { name, connectCost, bandwidth, canConnect }`.

Example:

```
@comm 2
BUS0 10 5 1 1 1
BUS1 2 1 1 1 0
```

BUS0: every resource may connect, connection costs 10 per resource,
5 data units per time unit. BUS1: cheap (2) but slow (1) and P2 cannot
use it.

## What is NOT part of the file

- **The makespan time limit (Tmax).** It is a parameter of the
  optimizer, supplied as a program argument (`--tmax N`), not a section
  of the ETG file.
- **Task repetition** from the ETG specification is not modeled
  (open question for the instructor).

## Validation summary

`parseETG` throws immediately on errors that make parsing impossible:

- file cannot be opened,
- a mandatory section is missing,
- task/processor count is not positive,
- a task token cannot be parsed (unknown category prefix or missing id).

`validateETG` collects ALL remaining problems at once and returns them
as a list of errors and warnings:

| Check | Severity |
|---|---|
| parsed task/processor count matches the section headers | error |
| `@times` / `@cost` have exactly N rows × M columns | error |
| every channel has exactly M connectivity flags | error |
| task ids unique and within `[0, N)` | error |
| successor ids within `[0, N)`, no self-loops | error |
| edge data volume non-negative | error |
| the graph is acyclic (cycle participants are listed) | error |
| every task has at least 1 allowed resource (category filter + sentinels) | error |
| a common task (CDT/CGT) has only 1 allowed resource | warning |
| `declaredSuccessors` equals the actual successor count | warning |

Program exit codes: `0` = file parsed and valid, `1` = open/parse/
validation error.

## Minimal valid example

```
@tasks 2
GT0 1 1(0)
GT1 0
@proc 1
0 0 1
@times
5
3
@cost
10
8
```

One universal resource, two tasks in a chain, no communication.