#include "etg.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <cctype>

namespace {

void rstrip(std::string& s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' ||
                          s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
}

std::vector<int> parseInts(const std::string& line) {
    std::vector<int> v;
    std::istringstream iss(line);
    int x;
    while (iss >> x) v.push_back(x);
    return v;
}

etg::Edge parseEdge(const std::string& tok) {
    const std::size_t open = tok.find('(');
    const std::size_t close = tok.find(')');
    etg::Edge e;
    if (open == std::string::npos) {
        e.to = std::stoi(tok);
        return e;
    }
    e.to = std::stoi(tok.substr(0, open));
    if (close != std::string::npos && close > open + 1)
        e.data = std::stoi(tok.substr(open + 1, close - open - 1));
    return e;
}

}

namespace etg {

// Split a task token "GT0" / "CDT3" / "DT8" / bare "T0" into category + id.
// The id is the trailing run of digits; the leading letters are the category.
bool parseTaskToken(const std::string& tok, Category& cat, int& id) {
    std::size_t p = 0;
    while (p < tok.size() && !std::isdigit(static_cast<unsigned char>(tok[p]))) ++p;
    if (p == 0 || p == tok.size()) return false; // no prefix or no number
    const std::string prefix = tok.substr(0, p);
    const std::string number = tok.substr(p);

    if      (prefix == "GT" || prefix == "T") cat = Category::GT;
    else if (prefix == "DT")  cat = Category::DT;
    else if (prefix == "UT")  cat = Category::UT;
    else if (prefix == "CDT") cat = Category::CDT;
    else if (prefix == "CGT") cat = Category::CGT;
    else return false;

    id = std::stoi(number);
    return true;
}

ETG parseETG(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open a file: " + path);

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) { rstrip(line); lines.push_back(line); }

    ETG g;
    bool foundTasks = false, foundProc = false, foundTimes = false, foundCost = false;
    std::size_t i = 0;
    while (i < lines.size()) {
        if (lines[i].empty()) { ++i; continue; }

        std::istringstream header(lines[i]);
        std::string tag;
        header >> tag;

        if (tag == "@tasks") {
            foundTasks = true;
            header >> g.numTasks;
            ++i;
            g.tasks.resize(g.numTasks);
            for (int t = 0; t < g.numTasks && i < lines.size(); ++t, ++i) {
                std::istringstream iss(lines[i]);
                std::vector<std::string> words;
                std::string w;
                while (iss >> w) words.push_back(w);
                if (words.empty()) { 
                    --t; 
                    continue; }

                Task task;
                Category cat;
                int tid;
                if (!parseTaskToken(words[0], cat, tid))
                    throw std::runtime_error("Cannot parse task token '" + words[0] + "'");
                task.id = tid;
                task.cat = cat;
                task.declaredSucc = (words.size() > 1) ? std::stoi(words[1]) : 0;
                for (std::size_t k = 2; k < words.size(); ++k)
                    task.successors.push_back(parseEdge(words[k]));

                if (task.id >= 0 && task.id < static_cast<int>(g.tasks.size()))
                    g.tasks[task.id] = task;
                else
                    g.tasks.push_back(task);
            }
        }
        else if (tag == "@proc") {
            foundProc = true;
            header >> g.numProcs;
            ++i;
            // raw[0] = purchase cost, raw[1] = reserved, raw[2] = type flag (see etg.h)
            for (int p = 0; p < g.numProcs && i < lines.size(); ++p, ++i) {
                Processor proc;
                proc.id = p;
                proc.raw = parseInts(lines[i]);
                g.procs.push_back(proc);
            }
        }
        else if (tag == "@times") {
            foundTimes = true;
            ++i;
            for (int t = 0; t < g.numTasks && i < lines.size(); ++t, ++i)
                g.times.push_back(parseInts(lines[i]));
        }
        else if (tag == "@cost") {
            foundCost = true;
            ++i;
            for (int t = 0; t < g.numTasks && i < lines.size(); ++t, ++i)
                g.costs.push_back(parseInts(lines[i]));
        }
        else if (tag == "@comm") {
            int nComm = 0;
            header >> nComm;
            ++i;
            for (int c = 0; c < nComm && i < lines.size(); ++c, ++i) {
                std::istringstream iss(lines[i]);
                CommChannel ch;
                iss >> ch.name >> ch.connectCost >> ch.bandwidth;
                int flag;
                while (iss >> flag) ch.canConnect.push_back(flag);
                g.channels.push_back(ch);
            }
        }
        else {
            ++i;
        }
    }

    if (!foundTasks) throw std::runtime_error("Missing section: @tasks");
    if (!foundProc)  throw std::runtime_error("Missing section: @proc");
    if (!foundTimes) throw std::runtime_error("Missing section: @times");
    if (!foundCost)  throw std::runtime_error("Missing section: @cost");
    if (g.numTasks <= 0) throw std::runtime_error("@tasks: task count must be > 0");
    if (g.numProcs <= 0) throw std::runtime_error("@proc: processor count must be > 0");

    return g;
}

const char* categoryName(Category c) {
    switch (c) {
        case Category::GT: return "GT";
        case Category::DT: return "DT";
        case Category::UT: return "UT";
        case Category::CDT: return "CDT";
        case Category::CGT: return "CGT";
    }
    return "?";
}

namespace {

// Same rule as the solver side: sentinel >= 0 in both matrices + category type filter.
bool assignmentAllowed(const ETG& g, int t, int p) {
    if (g.times[t][p] < 0 || g.costs[t][p] < 0) return false;
    switch (g.tasks[t].cat) {
        case Category::DT:
        case Category::CDT: return !g.procs[p].isUniversal();
        case Category::UT: return g.procs[p].isUniversal();
        default: return true;
    }
}

}

ValidationResult validateETG(const ETG& g) {
    ValidationResult r;
    auto err  = [&r](std::string m) { r.errors.push_back(std::move(m)); };
    auto warn = [&r](std::string m) { r.warnings.push_back(std::move(m)); };

    if (static_cast<int>(g.tasks.size()) != g.numTasks)
        err("@tasks declares " + std::to_string(g.numTasks) + " tasks, parsed " +
            std::to_string(g.tasks.size()));
    if (static_cast<int>(g.procs.size()) != g.numProcs)
        err("@proc declares " + std::to_string(g.numProcs) + " processors, parsed " +
            std::to_string(g.procs.size()));

    bool dimsOk = true;
    auto checkMatrix = [&](const std::vector<std::vector<int>>& m, const std::string& name) {
        if (static_cast<int>(m.size()) != g.numTasks) {
            err(name + " has " + std::to_string(m.size()) + " rows, expected " +
                std::to_string(g.numTasks));
            dimsOk = false;
        }
        for (std::size_t t = 0; t < m.size(); ++t)
            if (static_cast<int>(m[t].size()) != g.numProcs) {
                err(name + " row T" + std::to_string(t) + " has " +
                    std::to_string(m[t].size()) + " columns, expected " +
                    std::to_string(g.numProcs));
                dimsOk = false;
            }
    };
    checkMatrix(g.times, "@times");
    checkMatrix(g.costs, "@cost");

    for (const auto& ch : g.channels)
        if (static_cast<int>(ch.canConnect.size()) != g.numProcs)
            err("channel " + ch.name + " has " + std::to_string(ch.canConnect.size()) +
                " flags, expected " + std::to_string(g.numProcs));

    std::vector<int> seen(g.numTasks, 0);
    for (const auto& t : g.tasks) {
        if (t.id < 0 || t.id >= g.numTasks) {
            err("task id " + std::to_string(t.id) + " out of range [0, " +
                std::to_string(g.numTasks) + ")");
            continue;
        }
        if (++seen[t.id] > 1)
            err("duplicate task id T" + std::to_string(t.id));
        if (static_cast<int>(t.successors.size()) != t.declaredSucc)
            warn("T" + std::to_string(t.id) + " declares " + std::to_string(t.declaredSucc) +
                 " successors, found " + std::to_string(t.successors.size()));
        for (const auto& e : t.successors) {
            if (e.to == t.id)
                err("T" + std::to_string(t.id) + " has a self-loop");
            else if (e.to < 0 || e.to >= g.numTasks)
                err("T" + std::to_string(t.id) + " has successor T" + std::to_string(e.to) +
                    " out of range [0, " + std::to_string(g.numTasks) + ")");
            if (e.data < 0)
                err("edge T" + std::to_string(t.id) + " -> T" + std::to_string(e.to) +
                    " has negative data volume");
        }
    }

    bool acyclic = true;
    const std::vector<int> order = topoOrder(g, acyclic);
    if (!acyclic) {
        std::vector<char> inOrder(g.numTasks, 0);
        for (int v : order) inOrder[v] = 1;
        std::string who;
        for (int v = 0; v < g.numTasks; ++v)
            if (!inOrder[v]) who += (who.empty() ? "T" : ", T") + std::to_string(v);
        err("cycle detected, involved tasks (cycle + everything after it): " + who);
    }

    if (dimsOk) {
        for (int t = 0; t < g.numTasks; ++t) {
            int allowed = 0;
            for (int p = 0; p < g.numProcs; ++p)
                if (assignmentAllowed(g, t, p)) ++allowed;
            // Every task needs at least one feasible resource. Common tasks
            // (CDT/CGT) may use more, but the count is decided by the
            // optimizer, so a single feasible resource is enough to be valid.
            if (allowed < 1)
                err("T" + std::to_string(t) + " (" + categoryName(g.tasks[t].cat) +
                    ") has no allowed resource (forbidden by category filter "
                    "and/or matrix sentinels)");
            else if (g.tasks[t].isCommon() && allowed < 2)
                warn("T" + std::to_string(t) + " (" + categoryName(g.tasks[t].cat) +
                     ") is a common task but only 1 resource is allowed, so it "
                     "can never run on more than one resource");
        }
    }

    return r;
}

void validateOrThrow(const ETG& g) {
    const ValidationResult r = validateETG(g);
    if (r.ok()) return;
    std::string msg = "Invalid ETG file (" + std::to_string(r.errors.size()) + " error(s)):";
    for (const auto& e : r.errors) msg += "\n  - " + e;
    throw std::runtime_error(msg);
}

std::vector<std::vector<int>> buildPredecessors(const ETG& g) {
    std::vector<std::vector<int>> preds(g.numTasks);
    for (const auto& t : g.tasks)
        for (const auto& e : t.successors)
            if (e.to >= 0 && e.to < g.numTasks)
                preds[e.to].push_back(t.id);
    return preds;
}

std::vector<int> topoOrder(const ETG& g, bool& acyclic) {
    std::vector<int> indeg(g.numTasks, 0);
    for (const auto& t : g.tasks)
        for (const auto& e : t.successors)
            if (e.to >= 0 && e.to < g.numTasks) ++indeg[e.to];

    std::vector<int> ready, order;
    for (int v = 0; v < g.numTasks; ++v)
        if (indeg[v] == 0) ready.push_back(v);

    while (!ready.empty()) {
        const int v = ready.back();
        ready.pop_back();
        order.push_back(v);
        for (const auto& e : g.tasks[v].successors)
            if (e.to >= 0 && e.to < g.numTasks && --indeg[e.to] == 0)
                ready.push_back(e.to);
    }
    acyclic = (static_cast<int>(order.size()) == g.numTasks);
    return order;
}

void printSummary(const ETG& g, std::ostream& os) {
    os << "ETG: " << g.numTasks << " tasks, " << g.numProcs << " processors, "
       << g.channels.size() << " channel(s)\n";
    os << "times: " << g.times.size() << " x " << (g.times.empty() ? 0 : g.times[0].size())
       << ", cost: " << g.costs.size() << " x " << (g.costs.empty() ? 0 : g.costs[0].size()) << "\n\n";

    os << "Tasks (round-trip):\n";
    for (const auto& t : g.tasks) {
        // Print in the new token form: GT0, UT2, CDT3, ...
        os << categoryName(t.cat) << t.id;
        os << " " << t.successors.size();
        for (const auto& e : t.successors)
            os << " " << e.to << "(" << e.data << ")";
        if (static_cast<int>(t.successors.size()) != t.declaredSucc)
            os << "   [WARNING: declared " << t.declaredSucc << "]";
        os << "\n";
    }

    os << "\nProcessors:\n";
    for (const auto& p : g.procs) {
        os << "P" << p.id << " raw=[";
        for (std::size_t k = 0; k < p.raw.size(); ++k)
            os << p.raw[k] << (k + 1 < p.raw.size() ? "," : "");
        os << "] cost=" << p.cost() << " typeFlag=" << p.typeFlag() << "\n";
    }

    os << "\nChannels:\n";
    for (const auto& ch : g.channels)
        os << ch.name << " connectCost=" << ch.connectCost
           << " bandwidth=" << ch.bandwidth
           << " flags=" << ch.canConnect.size() << "\n";

    bool acyclic = true;
    const auto order = topoOrder(g, acyclic);
    const auto preds = buildPredecessors(g);

    os << "\nValidation:\n";
    os << "DAG: " << (acyclic ? "YES" : "NO - CYCLE DETECTED") << "\n";

    os << "roots:";
    for (int v = 0; v < g.numTasks; ++v) if (preds[v].empty()) os << " T" << v;
    os << "\nleaves:";
    for (int v = 0; v < g.numTasks; ++v) if (g.tasks[v].successors.empty()) os << " T" << v;
    os << "\ntopo:";
    for (int v : order) os << " T" << v;
    os << "\n";

    bool timesOk = true, costOk = true;
    for (const auto& r : g.times) if (static_cast<int>(r.size()) != g.numProcs) timesOk = false;
    for (const auto& r : g.costs) if (static_cast<int>(r.size()) != g.numProcs) costOk = false;
    os << "times: columns == processors: " << (timesOk ? "YES" : "NO") << "\n";
    os << "cost:  columns == processors: " << (costOk  ? "YES" : "NO") << "\n";
    for (const auto& ch : g.channels)
        os << ch.name << ": flags == processors: "
           << (static_cast<int>(ch.canConnect.size()) == g.numProcs ? "YES" : "NO") << "\n";
}

}