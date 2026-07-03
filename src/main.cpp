#include "etg.h"
#include "etg_prep.h"
#include "spanning_tree.h"
#include "ga.h"

#include <iostream>
#include <string>
#include <cstdlib>

static void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " [input_file] [--tmax N] [--seed N] [--lambda N]\n"
              << "  GA: [--alpha X] [--beta X] [--gamma X] [--delta X]\n"
              << "      [--rank-pressure X] [--no-improve N] [--max-gen N]\n";
}

static int readIntArg(int argc, char** argv, int& i, const char* name) {
    if (i + 1 >= argc) {
        std::cerr << "Missing value for " << name << "\n";
        return -1;
    }
    return std::atoi(argv[++i]);
}

static double readDoubleArg(int argc, char** argv, int& i, const char* name) {
    if (i + 1 >= argc) {
        std::cerr << "Missing value for " << name << "\n";
        return -1.0;
    }
    return std::atof(argv[++i]);
}

int main(int argc, char** argv)
{
    std::string path = "input.txt";
    solver::EvalParams evalParams;
    solver::GaParams gaParams;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--tmax") {
            int v = readIntArg(argc, argv, i, "--tmax");
            if (v < 0) return 1;
            evalParams.tmax = v;
        } else if (arg == "--seed") {
            int v = readIntArg(argc, argv, i, "--seed");
            if (v < 0) return 1;
            gaParams.seed = static_cast<unsigned>(v);
        } else if (arg == "--lambda") {
            int v = readIntArg(argc, argv, i, "--lambda");
            if (v < 0) return 1;
            evalParams.lambda = static_cast<double>(v);
        } else if (arg == "--alpha") {
            double v = readDoubleArg(argc, argv, i, "--alpha");
            if (v < 0.0) return 1;
            gaParams.alpha = v;
        } else if (arg == "--beta") {
            double v = readDoubleArg(argc, argv, i, "--beta");
            if (v < 0.0) return 1;
            gaParams.beta = v;
        } else if (arg == "--gamma") {
            double v = readDoubleArg(argc, argv, i, "--gamma");
            if (v < 0.0) return 1;
            gaParams.gamma = v;
        } else if (arg == "--delta") {
            double v = readDoubleArg(argc, argv, i, "--delta");
            if (v < 0.0) return 1;
            gaParams.delta = v;
        } else if (arg == "--rank-pressure") {
            double v = readDoubleArg(argc, argv, i, "--rank-pressure");
            if (v < 0.0) return 1;
            gaParams.rankPressure = v;
        } else if (arg == "--no-improve") {
            int v = readIntArg(argc, argv, i, "--no-improve");
            if (v < 0) return 1;
            gaParams.noImproveLimit = v;
        } else if (arg == "--max-gen") {
            int v = readIntArg(argc, argv, i, "--max-gen");
            if (v < 0) return 1;
            gaParams.maxGenerations = v;
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
        const solver::SpanningTree tree = solver::buildSpanningTree(prep);

        etg::printSummary(graph, std::cout);

        const int tau = solver::countPeTypes(graph);
        std::cout << "\nGA parameters:\n";
        std::cout << "  alpha: " << gaParams.alpha
                  << "  beta: " << gaParams.beta
                  << "  gamma: " << gaParams.gamma
                  << "  delta: " << gaParams.delta << "\n";
        std::cout << "  population: round(alpha*n*tau) = "
                  << static_cast<int>(gaParams.alpha * graph.numTasks * tau)
                  << "  (n=" << graph.numTasks << ", tau=" << tau << ")\n";
        std::cout << "  rank pressure: " << gaParams.rankPressure << "\n";
        std::cout << "  stop: no improvement for " << gaParams.noImproveLimit
                  << " generation(s), max " << gaParams.maxGenerations << "\n";
        std::cout << "  Tmax: " << (evalParams.tmax > 0 ? std::to_string(evalParams.tmax) : "(not set)") << "\n";
        std::cout << "  seed: " << gaParams.seed << "\n";

        const solver::GaResult result = solver::runGa(
            graph, prep, tree, gaParams, evalParams);

        std::cout << "\nBest solution after " << result.generationsRun << " generation(s)";
        if (result.stoppedByNoImprove)
            std::cout << " (stopped: no improvement)";
        std::cout << ":\n";
        solver::printSchedule(result.best, graph, std::cout);
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
