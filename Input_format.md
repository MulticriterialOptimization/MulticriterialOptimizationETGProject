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
  count is needed to read the matrices. `@type` is fully order-independent.
- Mandatory sections: `@tasks`, `@proc`, `@times`, `@cost`.
  Optional sections: `@type`, `@comm`.
- Task ids are `0 .. numTasks-1`, resource (processor) ids are
  `0 .. numProcs-1`. Resource ids are implicit: the k-th row of `@proc`
  is resource `Pk`.

## `@tasks` — the task graph (mandatory)

```
@tasks N
T<id> <declaredSuccessors> [succ1(data1)] [succ2(data2)] ...
```

- `N` — number of tasks; exactly `N` task lines follow.
- `T<id>` — task identifier (`T` + integer).
- `<declaredSuccessors>` — declared number of successors. Used as a
  consistency check only: a mismatch with the actual edge list produces
  a WARNING, not an error (the edge list is the source of truth).
- `succ(data)` — a directed edge to task `T<succ>` carrying `data`
  units of data. `data = 0` means a pure precedence constraint with no
  communication. A successor without parentheses means `data = 0`.

Maps to: `struct Task { id, declaredSucc, cat, width, successors }`,
where each edge is `struct Edge { to, data }`.

The graph must be a DAG (no cycles, no self-loops) — checked by
`validateETG`.

Example:

```
@tasks 3
T0 2 1(10) 2(0)
T1 1 2(5)
T2 0
```

T0 has two successors: T1 (10 units of data) and T2 (precedence only).
T1 sends 5 units of data to T2. T2 is the exit task.

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

## `@type` — task categories (optional)

```
@type
T<id> <CATEGORY> [width]
```

Tasks not listed here default to `GT`, so legacy files without this
section remain fully valid.

| Category | Resources allowed | Width |
|---|---|---|
| `GT`  | one resource of any type | 1 |
| `DT`  | one SPECIALIZED resource (sentinels may narrow it to specific ones) | 1 |
| `UT`  | one UNIVERSAL resource | 1 |
| `CDT` | `width` SPECIALIZED resources working simultaneously | given (default 2) |
| `CGT` | `width` resources of any type working simultaneously | given (default 2) |

Rules:

- `width` is only valid for `CDT`/`CGT`; for `GT`/`DT`/`UT` it is
  always 1 (an explicit `width > 1` is an error).
- The effective set of allowed resources for a task is the intersection
  of the category type filter and the matrix sentinels. If it has fewer
  than `width` elements, the file is rejected
  (`T3 (CDT, width 4) has only 3 allowed resource(s)`).

Common task semantics (CDT/CGT, assumptions to confirm with the
instructor): the task needs exactly `width` resources at the same time.
All of them start together and stay busy until the task finishes;
duration = the MAXIMUM of the chosen resources' execution times (the
slowest participant determines completion); execution cost = the SUM of
the chosen resources' costs. A specialized resource taking part in a
common task consumes its single task slot.

Example:

```
@type
T1 UT
T2 DT
T3 CDT 2
T4 CGT 3
```

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
- `@type` refers to an unknown task, uses an unknown category name,
  or sets an invalid width.

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
| every task has at least `width` allowed resources (category filter + sentinels) | error |
| `declaredSuccessors` equals the actual successor count | warning |

Program exit codes: `0` = file parsed and valid, `1` = open/parse/
validation error.

## Minimal valid example

```
@tasks 2
T0 1 1(0)
T1 0
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
