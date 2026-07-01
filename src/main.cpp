#include "etg.h"
#include "etg_prep.h"

#include <iostream>
#include <string>
#include <cstdlib>

static void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " [input_file] [--tmax N]\n";
}

int main(int argc, char** argv)
{
    std::string path = "input.txt";
    int tmax = -1;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tmax" && i + 1 < argc) {
            tmax = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg[0] != '-') {
            path = arg;
        }
    }

    try {
        const etg::ETG graph = etg::parseETG(path);

        const etg::ValidationResult vr = etg::validateETG(graph);
        for (const auto& w : vr.warnings)
            std::cerr << "WARNING: " << w << '\n';
        if (!vr.ok()) {
            std::cerr << "Invalid ETG file (" << vr.errors.size() << " error(s)):\n";
            for (const auto& e : vr.errors)
                std::cerr << "  - " << e << '\n';
            return 1;
        }

        const etg::PreparedData prep = etg::prepare(graph);

        etg::printSummary(graph, std::cout);

        std::cout << "\nPrepared data:\n";
        std::cout << "  Tmax: " << (tmax > 0 ? std::to_string(tmax) : "(not set)") << "\n";
        std::cout << "  Topological order:";
        for (int t : prep.topo) std::cout << " T" << t;
        std::cout << "\n";
        for (int t = 0; t < graph.numTasks; ++t) {
            std::cout << "  T" << t << " allowed:";
            for (int p : prep.allowed[t]) std::cout << " P" << p;
            std::cout << " | nSucc=" << prep.nSucc[t] << "\n";
        }

        // TODO: solver goes here (Osoba 2 + Osoba 3)
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
