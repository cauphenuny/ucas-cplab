#pragma once

#include "backend/rv64/inst.hpp"
#include "framework.hpp"

namespace rv64::optim {

struct RedundantJumpElimination : Pass {
    bool apply(Module& mod) override {
        bool changed = false;
        for (auto& func : mod.funcs) {
            for (size_t i = 0; i + 1 < func.blocks.size(); i++) {
                auto last = func.blocks[i].insts.back();
                if (auto j = std::get_if<PseudoJ>(&last)) {
                    if (j->op == PseudoJ::J && j->target == func.blocks[i + 1].label) {
                        func.blocks[i].insts.pop_back();
                        changed = true;
                    }
                }
            }
        }
        return changed;
    }
};

struct BranchCondSimplification : Pass {
    // TODO: convert xxx + bnez to bxx
};

}  // namespace rv64::optim