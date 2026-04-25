/// @brief Dead Definition Elimination Pass, requires SSA

#pragma once
#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "framework.hpp"

#include <list>

namespace ir::optim {

struct DeadDefElimination : SSAPass {
    bool apply(Program& prog, SSAPassContext& ctx) override {
        if (!prog.is_ssa) {
            throw COMPILER_ERROR("DeadDefElimination requires SSA form");
        }
        bool result = false;
        while (eliminate(prog, ctx)) result = true;
        return result;
    }

private:
    bool eliminate(Program& prog, SSAPassContext& ctx) {
        bool changed = false;
        for (auto& func : prog.funcs()) {
            for (auto& block : func->blocks()) {
                for (auto it = block->insts().begin(); it != block->insts().end();) {
                    auto next = std::next(it);
                    auto& inst = *it;
                    auto def = utils::defined_var(inst);
                    if (def && ctx.ud.uses_of(*def).empty() && !utils::has_side_effect(inst)) {
                        block->erase(it);
                        changed = true;
                    }
                    it = next;
                }
            }
        }
        return changed;
    }
};

}  // namespace ir::optim