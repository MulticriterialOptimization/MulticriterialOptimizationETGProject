#include "evaluator.h"

#include <algorithm>
#include <vector>

namespace solver {

namespace {

const double kBadScore = 1e15;

int ceilDiv(int data, int bandwidth) {
    if (bandwidth <= 0)
        return 0;
    return (data + bandwidth - 1) / bandwidth;
}

struct EvalState {
    std::vector<double> freeAt;
    std::vector<double> lastFinish;
    std::vector<int> peUseCount;
    std::vector<bool> usedProcs;
    std::vector<std::vector<bool>> channelConnected;
    std::vector<int> channelUseCount;
    std::vector<double> channelLastFinish;
    std::vector<TaskAssignment> assignments;
    double commCost = 0.0;
};

bool isProcAvailable(const etg::ETG& graph, int p, const EvalState& st) {
    (void)graph;
    (void)p;
    (void)st;
    return true;
}

std::vector<int> availableCandidates(
    const etg::ETG& graph,
    const etg::PreparedData& prep,
    int taskId,
    const EvalState& st)
{
    std::vector<int> out;
    for (int p : prep.allowed[taskId]) {
        if (isProcAvailable(graph, p, st))
            out.push_back(p);
    }
    return out;
}

std::vector<int> filterUsed(const std::vector<int>& pool, const EvalState& st) {
    std::vector<int> out;
    for (int p : pool) {
        if (st.usedProcs[p])
            out.push_back(p);
    }
    return out;
}

double procExecCost(const etg::ETG& graph, int taskId, int procId, const EvalState& st) {
    double c = static_cast<double>(graph.costs[taskId][procId]);
    if (!st.usedProcs[procId])
        c += static_cast<double>(graph.procs[procId].cost());
    return c;
}

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

double commonExecCost(const etg::ETG& graph, int taskId, const std::vector<int>& procs,
                      const EvalState& st)
{
    int k = static_cast<int>(procs.size());
    double sum = 0.0;
    for (int p : procs) {
        sum += static_cast<double>(graph.costs[taskId][p]) / static_cast<double>(k);
        if (!st.usedProcs[p])
            sum += static_cast<double>(graph.procs[p].cost());
    }
    return sum;
}

bool channelLegal(const etg::ETG& graph, int channelId, int sender, int receiver) {
    const etg::CommChannel& ch = graph.channels[channelId];
    if (sender < 0 || receiver < 0)
        return false;
    if (sender >= static_cast<int>(ch.canConnect.size()) ||
        receiver >= static_cast<int>(ch.canConnect.size()))
        return false;
    return ch.canConnect[sender] != 0 && ch.canConnect[receiver] != 0;
}

bool channelAllocated(const EvalState& st, int channelId, int sender, int receiver) {
    return st.channelConnected[channelId][sender] &&
           st.channelConnected[channelId][receiver];
}

double channelMarginalCost(const etg::ETG& graph, const EvalState& st,
                           int channelId, int sender, int receiver)
{
    double cost = 0.0;
    const etg::CommChannel& ch = graph.channels[channelId];
    if (!st.channelConnected[channelId][sender])
        cost += static_cast<double>(ch.connectCost);
    if (!st.channelConnected[channelId][receiver])
        cost += static_cast<double>(ch.connectCost);
    return cost;
}

int pickChannel(ClsGene gene, const etg::ETG& graph, const EvalState& st,
                int sender, int receiver)
{
    std::vector<int> legal;
    for (int c = 0; c < static_cast<int>(graph.channels.size()); ++c) {
        if (channelLegal(graph, c, sender, receiver))
            legal.push_back(c);
    }
    if (legal.empty())
        return -1;

    std::vector<int> pool = legal;
    if (gene == ClsGene::ClsAllocCheapest || gene == ClsGene::ClsAllocFastest ||
        gene == ClsGene::ClsAllocLFU || gene == ClsGene::ClsAllocIdle)
    {
        std::vector<int> allocated;
        for (int c : legal) {
            if (channelAllocated(st, c, sender, receiver))
                allocated.push_back(c);
        }
        if (!allocated.empty())
            pool = allocated;
        else
            gene = ClsGene::ClsCheapest;
    }

    int best = pool[0];
    double bestScore = kBadScore;

    for (int c : pool) {
        double score = 0.0;
        const etg::CommChannel& ch = graph.channels[c];

        switch (gene) {
            case ClsGene::ClsAllocCheapest:
            case ClsGene::ClsCheapest:
                score = static_cast<double>(ch.connectCost) +
                        channelMarginalCost(graph, st, c, sender, receiver);
                break;
            case ClsGene::ClsAllocFastest:
            case ClsGene::ClsHighestBw:
                score = -static_cast<double>(ch.bandwidth);
                break;
            case ClsGene::ClsAllocLFU:
            case ClsGene::ClsLFU:
                score = static_cast<double>(st.channelUseCount[c]);
                break;
            case ClsGene::ClsAllocIdle:
                score = st.channelLastFinish[c];
                break;
            default:
                break;
        }

        if (score < bestScore) {
            bestScore = score;
            best = c;
        }
    }

    return best;
}

double applyTransfer(EvalState& st, const etg::ETG& graph, ClsGene gene,
                     int sender, int receiver, int dataVolume, double readyTime)
{
    if (dataVolume <= 0 || sender == receiver)
        return 0.0;

    if (graph.channels.empty())
        return 0.0;

    int channelId = pickChannel(gene, graph, st, sender, receiver);
    if (channelId < 0)
        return 0.0;

    st.commCost += channelMarginalCost(graph, st, channelId, sender, receiver);
    st.channelConnected[channelId][sender] = true;
    st.channelConnected[channelId][receiver] = true;

    const etg::CommChannel& ch = graph.channels[channelId];
    double transferTime = static_cast<double>(ceilDiv(dataVolume, ch.bandwidth));
    double finish = readyTime + transferTime;

    st.channelUseCount[channelId] += 1;
    if (finish > st.channelLastFinish[channelId])
        st.channelLastFinish[channelId] = finish;

    return transferTime;
}

int edgeDataVolume(const etg::ETG& graph, int fromTask, int toTask) {
    for (const etg::Edge& e : graph.tasks[fromTask].successors) {
        if (e.to == toTask)
            return e.data;
    }
    return 0;
}

double predecessorsReadyTime(
    const etg::ETG& graph,
    const etg::PreparedData& prep,
    EvalState& st,
    int taskId,
    const std::vector<int>& consProcs,
    ClsGene clsGene)
{
    double ready = 0.0;

    for (int predId : prep.preds[taskId]) {
        const TaskAssignment& predAssign = st.assignments[predId];
        int data = edgeDataVolume(graph, predId, taskId);

        if (data <= 0) {
            if (predAssign.finishTime > ready)
                ready = predAssign.finishTime;
            continue;
        }

        int kProd = static_cast<int>(predAssign.procIds.size());
        if (kProd <= 0)
            continue;

        double edgeFinish = predAssign.finishTime;

        for (int sender : predAssign.procIds) {
            int piece = data / kProd;
            for (int receiver : consProcs) {
                if (sender == receiver)
                    continue;
                double transfer = applyTransfer(
                    st, graph, clsGene, sender, receiver, piece, predAssign.finishTime);
                double arrival = predAssign.finishTime + transfer;
                if (arrival > edgeFinish)
                    edgeFinish = arrival;
            }
        }

        if (edgeFinish > ready)
            ready = edgeFinish;
    }

    return ready;
}

double procReadyTime(const EvalState& st, const std::vector<int>& procs) {
    double ready = 0.0;
    for (int p : procs) {
        if (st.freeAt[p] > ready)
            ready = st.freeAt[p];
    }
    return ready;
}

void markProcUsed(const etg::ETG& graph, EvalState& st, int p, double finishTime) {
    (void)graph;
    st.usedProcs[p] = true;
    st.peUseCount[p] += 1;
    st.lastFinish[p] = finishTime;
    st.freeAt[p] = finishTime; // every resource runs its tasks sequentially
}

void markProcsUsed(const etg::ETG& graph, EvalState& st, const std::vector<int>& procs,
                   double finishTime)
{
    for (int p : procs)
        markProcUsed(graph, st, p, finishTime);
}

bool pickSameAsPred(
    const etg::ETG& graph,
    const etg::PreparedData& prep,
    const SpanningTree& tree,
    int taskId,
    const EvalState& st,
    int& outProc)
{
    int parent = tree.parent[taskId];
    if (parent < 0)
        return false;

    const TaskAssignment& parentAssign = st.assignments[parent];
    for (int p : parentAssign.procIds) {
        bool ok = false;
        for (int allowed : prep.allowed[taskId]) {
            if (allowed == p) {
                ok = true;
                break;
            }
        }
        if (!ok)
            continue;
        if (!isProcAvailable(graph, p, st))
            continue;
        outProc = p;
        return true;
    }
    return false;
}

int pickSingleProc(PeGene gene, const etg::ETG& graph, const etg::PreparedData& prep,
                   const SpanningTree& tree, int taskId, const EvalState& st,
                   const std::vector<int>& pool)
{
    if (pool.empty())
        return -1;

    if (gene == PeGene::AllocSamePred) {
        int p = -1;
        if (pickSameAsPred(graph, prep, tree, taskId, st, p))
            return p;
        return pickSingleProc(PeGene::Cheapest, graph, prep, tree, taskId, st, pool);
    }

    int best = pool[0];
    double bestScore = kBadScore;

    for (int p : pool) {
        double score = 0.0;

        switch (gene) {
            case PeGene::AllocCheapest:
            case PeGene::Cheapest:
                score = procExecCost(graph, taskId, p, st);
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
                score = st.lastFinish[p];
                break;
            default:
                score = procExecCost(graph, taskId, p, st);
                break;
        }

        if (score < bestScore) {
            bestScore = score;
            best = p;
        }
    }

    return best;
}

double scoreCommonSubset(PeGene gene, const etg::ETG& graph, int taskId,
                         const std::vector<int>& subset, const EvalState& st)
{
    double execTime = commonExecTime(graph, taskId, subset);
    double execCost = commonExecCost(graph, taskId, subset, st);

    switch (gene) {
        case PeGene::AllocCheapest:
        case PeGene::Cheapest:
            return execCost;
        case PeGene::AllocFastest:
        case PeGene::Fastest:
            return execTime;
        case PeGene::MinTS:
            return execTime * execCost;
        case PeGene::AllocLFU: {
            double sum = 0.0;
            for (int p : subset)
                sum += static_cast<double>(st.peUseCount[p]);
            return sum;
        }
        case PeGene::AllocIdle: {
            double best = kBadScore;
            for (int p : subset) {
                if (st.lastFinish[p] < best)
                    best = st.lastFinish[p];
            }
            return best;
        }
        default:
            return execCost;
    }
}

std::vector<int> pickCommonProcs(PeGene gene, const etg::ETG& graph,
                                 const etg::PreparedData& prep,
                                 const SpanningTree& tree, int taskId,
                                 const EvalState& st, std::vector<int> pool)
{
    std::vector<int> result;
    if (pool.empty())
        return result;

    if (gene == PeGene::AllocCheapest || gene == PeGene::AllocFastest ||
        gene == PeGene::AllocLFU || gene == PeGene::AllocIdle ||
        gene == PeGene::AllocSamePred)
    {
        std::vector<int> used = filterUsed(pool, st);
        if (!used.empty())
            pool = used;
        else
            gene = PeGene::Cheapest;
    }

    if (gene == PeGene::AllocSamePred) {
        int p = -1;
        if (pickSameAsPred(graph, prep, tree, taskId, st, p)) {
            result.push_back(p);
            return result;
        }
        gene = PeGene::Cheapest;
    }

    int n = static_cast<int>(pool.size());
    double bestScore = kBadScore;

    for (int mask = 1; mask < (1 << n); ++mask) {
        std::vector<int> subset;
        for (int i = 0; i < n; ++i) {
            if (mask & (1 << i))
                subset.push_back(pool[i]);
        }

        double score = scoreCommonSubset(gene, graph, taskId, subset, st);
        if (score < bestScore) {
            bestScore = score;
            result = subset;
        }
    }

    return result;
}

std::vector<int> pickProcs(PeGene gene, const etg::ETG& graph,
                           const etg::PreparedData& prep, const SpanningTree& tree,
                           int taskId, const EvalState& st)
{
    std::vector<int> candidates = availableCandidates(graph, prep, taskId, st);
    if (candidates.empty())
        return {};

    if (graph.tasks[taskId].isCommon())
        return pickCommonProcs(gene, graph, prep, tree, taskId, st, candidates);

    std::vector<int> pool = candidates;
    if (gene == PeGene::AllocCheapest || gene == PeGene::AllocFastest ||
        gene == PeGene::AllocLFU || gene == PeGene::AllocIdle ||
        gene == PeGene::AllocSamePred)
    {
        std::vector<int> used = filterUsed(pool, st);
        if (!used.empty())
            pool = used;
        else
            gene = PeGene::Cheapest;
    }

    int p = pickSingleProc(gene, graph, prep, tree, taskId, st, pool);
    if (p < 0)
        return {};

    std::vector<int> one;
    one.push_back(p);
    return one;
}

} // namespace

void evaluateIndividual(
    Individual& ind,
    const etg::ETG& graph,
    const etg::PreparedData& prep,
    const SpanningTree& tree,
    const EvalParams& params)
{
    EvalState st;
    st.freeAt.assign(graph.numProcs, 0.0);
    st.lastFinish.assign(graph.numProcs, 0.0);
    st.peUseCount.assign(graph.numProcs, 0);
    st.usedProcs.assign(graph.numProcs, false);
    st.channelUseCount.assign(graph.channels.size(), 0);
    st.channelLastFinish.assign(graph.channels.size(), 0.0);
    st.assignments.assign(graph.numTasks, TaskAssignment());
    st.channelConnected.assign(
        graph.channels.size(),
        std::vector<bool>(graph.numProcs, false));
    st.commCost = 0.0;

    for (int t = 0; t < graph.numTasks; ++t)
        st.assignments[t].taskId = t;

    for (int taskId : prep.topo) {
        std::vector<int> procs = pickProcs(
            ind.peGenes[taskId], graph, prep, tree, taskId, st);

        if (procs.empty()) {
            ind.schedule.valid = false;
            ind.schedule.totalCost = params.penalty;
            ind.schedule.makespan = params.penalty;
            ind.fitness = params.penalty;
            return;
        }

        double commBefore = st.commCost;
        double dataReady = predecessorsReadyTime(
            graph, prep, st, taskId, procs, ind.clsGenes[taskId]);
        double commForTask = st.commCost - commBefore;

        TaskAssignment assign;
        assign.taskId = taskId;
        assign.procIds = procs;

        if (graph.tasks[taskId].isCommon()) {
            // Each processor computes its 1/k share in parallel, starting as
            // soon as it is free and the data is ready; the task (and its
            // output data) completes when the slowest share finishes.
            double execCost = commonExecCost(graph, taskId, procs, st);
            int k = static_cast<int>(procs.size());
            double firstStart = -1.0;
            double lastPieceFinish = 0.0;

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
                markProcUsed(graph, st, p, pieceFinish);
            }

            assign.startTime = firstStart;
            assign.finishTime = lastPieceFinish;
            assign.cost = execCost + commForTask;
        } else {
            double execCost = procExecCost(graph, taskId, procs[0], st);
            double start = dataReady;
            double procReady = procReadyTime(st, procs);
            if (procReady > start)
                start = procReady;

            assign.startTime = start;
            assign.finishTime = start + static_cast<double>(graph.times[taskId][procs[0]]);
            assign.cost = execCost + commForTask;
            markProcsUsed(graph, st, procs, assign.finishTime);
        }

        st.assignments[taskId] = assign;
    }

    ind.schedule.assignments = st.assignments;
    ind.schedule.valid = true;
    ind.schedule.totalCost = 0.0;
    ind.schedule.makespan = 0.0;

    for (const TaskAssignment& a : ind.schedule.assignments) {
        ind.schedule.totalCost += a.cost;
        if (a.finishTime > ind.schedule.makespan)
            ind.schedule.makespan = a.finishTime;
    }

    if (params.tmax > 0 &&
        ind.schedule.makespan > static_cast<double>(params.tmax))
    {
        ind.fitness = ind.schedule.totalCost +
            params.lambda * (ind.schedule.makespan - static_cast<double>(params.tmax));
    } else {
        ind.fitness = ind.schedule.totalCost;
    }
}

}
