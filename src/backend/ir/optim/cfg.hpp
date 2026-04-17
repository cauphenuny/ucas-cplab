#pragma once
#include "backend/ir/ir.hpp"

#include <unordered_map>
#include <unordered_set>

namespace ir::optim {

struct ControlFlowGraph {
    using Node = Block*;
    using NodeSet = std::unordered_set<Node>;
    std::unordered_map<Node, NodeSet> succ;
    std::unordered_map<Node, NodeSet> pred;

    Func& func;

    ControlFlowGraph(Func& func) : func(func) {
        for (const auto& blk_box : func.blocks()) {
            Node blk = blk_box.get();
            match(
                blk->exit(), [](const ReturnExit& ret) {},
                [&](const JumpExit& jump) { connect(blk, jump.target); },
                [&](const BranchExit& branch) {
                    connect(blk, branch.true_target);
                    connect(blk, branch.false_target);
                });
        }
    }

private:
    void connect(Node from, Node to) {
        succ[from].insert(to);
        pred[to].insert(from);
    }
};

}  // namespace ir::optim