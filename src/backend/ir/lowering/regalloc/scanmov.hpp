#pragma once

#include "backend/ir/ir.h"
#include "utils/match.hpp"

#include <unordered_set>
#include <unordered_map>
#include <string>

namespace ir::lowering {

struct MoveGraph {
    using Node = std::unordered_set<LeftValue>;
    std::unordered_map<LeftValue, Node> adj;
    [[nodiscard]] std::string toString() const {
        std::string res = "{";
        for (const auto& [value, neighbors] : adj) {
            res += fmt::format("{}: {}, ", value, neighbors);
        }
        res.pop_back(), res.pop_back();
        res += "}";
        return res;
    }
};

inline MoveGraph scan_move(const Program& prog, bool bidirectional = true) {
    MoveGraph graph;
    for (const auto& func : prog.funcs()) {
        for (const auto& block : func->blocks()) {
            for (const auto& inst : block->insts()) {
                if (auto unary = std::get_if<UnaryInst>(&inst)) {
                    if (unary->op == UnaryInstOp::MOV) {
                        match(
                            unary->operand,
                            [&](const LeftValue& lv) {
                                graph.adj[unary->result].insert(lv);
                                if (bidirectional) graph.adj[lv].insert(unary->result);
                            },
                            [&](const ConstexprValue& v) {
                                // Ignore non-LeftValue sources (e.g. immediates)
                            });
                    }
                }
            }
        }
    }
    return graph;
}

}  // namespace ir::lowering