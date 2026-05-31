#pragma once

#include "backend/ir/ir.h"
#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/lowering/regalloc/colorize.hpp"
#include "backend/ir/lowering/regalloc/interfere.hpp"
#include "backend/ir/lowering/regalloc/precolorize.hpp"
#include "backend/ir/lowering/regalloc/scanmov.hpp"
#include "backend/ir/lowering/regalloc/spill.hpp"
#include "backend/ir/transform/framework.hpp"

#include <unordered_map>
#include <utility>
#include <vector>

namespace ir::lowering {

struct RegisterAllocation : transform::NonSSAPass {
    using TargetABI = lowering::TargetABI;
    RegisterAllocation(TargetABI abi) : abi(std::move(abi)) {}
    TargetABI abi;

    bool apply(Program& prog, transform::NonSSAPassContext& ctx) override {
        PreColorize precolor(abi);
        precolor.apply(prog, ctx);
        auto precolored = precolor.precolored;

        while (true) {
            auto graph = InterfereGraph::build(prog, precolored, abi);
            auto moves = scan_move(prog);
            auto [spills, colors] = BriggsAllocator(graph, moves).colorize();
            if (spills.empty()) {
                this->colored = std::move(colors);
                break;
            }
            Spill(spills).apply(prog, ctx);
        }

        return true;
    }

    ColorMap colored;
};

}  // namespace ir::lowering