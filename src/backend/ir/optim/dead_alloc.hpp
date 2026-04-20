/// @brief dead allocation elimination

#pragma once

#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "framework.hpp"

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ir::optim {
struct DeadAllocElimination : Pass {
    bool apply(Program& prog) override {
        if (!prog.is_ssa) {
            throw COMPILER_ERROR("DeadAllocElimination requires SSA form");
        }
        bool changed = false;
        for (auto& func : prog.getFuncs()) {
            std::unordered_map<const Alloc*, bool> used;
            for (auto use_var : analysis::utils::used_vars(*func)) {
                if (auto named = std::get_if<NamedValue>(use_var); named) {
                    if (auto alloc = std::get_if<const Alloc*>(&named->def); alloc) {
                        used[*alloc] = true;
                    }
                }
                if (auto ssa = std::get_if<SSAValue>(use_var); ssa) {
                    used[ssa->def] = true;
                }
            }
            std::vector<std::unique_ptr<Alloc>> kept_locals;
            for (auto& alloc : func->locals()) {
                if (used.count(alloc.get())) {
                    kept_locals.push_back(std::move(alloc));
                } else {
                    changed = true;
                }
            }
            func->locals() = std::move(kept_locals);
        }
        return changed;
    }
};

}  // namespace ir::optim