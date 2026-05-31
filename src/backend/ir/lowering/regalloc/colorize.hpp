/// @brief Chaitin-Briggs Graph Coloring Register Allocator

#pragma once
#include "backend/ir/ir.h"
#include "backend/ir/lowering/regalloc/interfere.hpp"
#include "backend/ir/lowering/regalloc/precolorize.hpp"
#include "backend/ir/lowering/regalloc/scanmov.hpp"

#include <utility>
#include <vector>
#include <unordered_set>

namespace ir::lowering {

struct ColorizeResult {
    std::vector<LeftValue> spilled;
    ColorMap colors;
};

struct BriggsAllocator {
    InterfereGraph& graph;
    MoveGraph moves;
    struct {
        std::unordered_set<LeftValue> simplify;
        std::unordered_set<LeftValue> freeze;
        std::unordered_set<LeftValue> spill;
    } worklist;
    std::unordered_set<LeftValue> precolored;

    BriggsAllocator(InterfereGraph& graph, MoveGraph moves)
        : graph(graph), moves(std::move(moves)) {
    }

    ColorizeResult colorize() && {
        ColorizeResult result;
        for (const auto& [value, node] : graph.nodes()) {
            if (node.color) {
                precolored.insert(value);
                continue;
            }
            // TODO:
        }
        return result;
    }
};

}  // namespace ir::lowering