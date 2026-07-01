#ifndef GP_TREE_H
#define GP_TREE_H

#include <memory>
#include <vector>
#include <random>

namespace gp {

enum class FuncType { ADD, SUB, MUL, DIV, MIN, MAX, COUNT };
enum class TermType { COST, TIME, BUY_COST, N_SUCC, FREE_AT, CONST, COUNT };

constexpr int FUNC_COUNT = static_cast<int>(FuncType::COUNT);
constexpr int TERM_COUNT = static_cast<int>(TermType::COUNT);
struct EvalContext {
    double cost    = 0.0;
    double time    = 0.0;
    double buyCost = 0.0;
    double nSucc   = 0.0;
    double freeAt  = 0.0;
};

struct Node {
    bool isFunc = false;
    FuncType func = FuncType::ADD;  // used when isFunc == true
    TermType term = TermType::COST; // used when isFunc == false
    double constVal = 0.0;          // used when term == CONST
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;
};

// --- Tree construction helpers ---

std::unique_ptr<Node> makeFunc(FuncType f, std::unique_ptr<Node> l, std::unique_ptr<Node> r);
std::unique_ptr<Node> makeTerm(TermType t);
std::unique_ptr<Node> makeConst(double val);

// --- Core operations ---

double evaluate(const Node& node, const EvalContext& ctx);
int depth(const Node& node);
int size(const Node& node);
std::unique_ptr<Node> clone(const Node& node);
void collectNodes(Node& root, std::vector<Node*>& out);

// --- Tree generation ---

std::unique_ptr<Node> generateFull(int maxDepth, std::mt19937& rng);
std::unique_ptr<Node> generateGrow(int maxDepth, std::mt19937& rng);
std::vector<std::unique_ptr<Node>> rampedHalfAndHalf(
    int popSize, int minDepth, int maxDepth, std::mt19937& rng);

}

#endif
