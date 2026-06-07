#include "etg.h"

#include <fstream>
#include <sstream>
#include <iostream>
#include <stdexcept>

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
    const std::size_t open  = tok.find('(');
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

ETG parseETG(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Cannot open a file: " + path);

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) { rstrip(line); lines.push_back(line); }

    ETG g;
    std::size_t i = 0;
    while (i < lines.size()) {
        if (lines[i].empty()) { ++i; continue; }

        std::istringstream header(lines[i]);
        std::string tag;
        header >> tag;

        if (tag == "@tasks") {
            header >> g.numTasks;
            ++i;
            g.tasks.resize(g.numTasks);
            for (int t = 0; t < g.numTasks && i < lines.size(); ++t, ++i) {
                std::istringstream iss(lines[i]);
                std::vector<std::string> words;
                std::string w;
                while (iss >> w) words.push_back(w);
                if (words.empty()) { --t; continue; }

                Task task;
                task.id = std::stoi(words[0].substr(1));
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
            header >> g.numProcs;
            ++i;
            // Contain all numbers from the row, because not sure what 2 and 3 column means 
            for (int p = 0; p < g.numProcs && i < lines.size(); ++p, ++i) {
                Processor proc;
                proc.id = p;
                proc.raw = parseInts(lines[i]);
                g.procs.push_back(proc);
            }
        }
        else if (tag == "@times") {
            ++i;
            for (int t = 0; t < g.numTasks && i < lines.size(); ++t, ++i)
                g.times.push_back(parseInts(lines[i]));
        }
        else if (tag == "@cost") {
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
    return g;
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
        os << "T" << t.id << " " << t.successors.size();
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
