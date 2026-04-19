/// @brief dead allocation elimination

#pragma once

#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "framework.hpp"

#include <unordered_map>
#include <vector>

namespace ir::optim {
struct DeadAllocElimination : Pass {
    bool apply(Program& prog) override {
        bool changed = false;
        for (auto& func : prog.getFuncs()) {
            std::unordered_map<const Alloc*, bool> used;
            for (auto& block : func->blocks()) {
                auto track = [&](LeftValue* v) {
                    if (auto named = std::get_if<NamedValue>(v); named) {
                        if (auto alloc = std::get_if<const Alloc*>(&named->def); alloc) {
                            used[*alloc] = true;
                        }
                    }
                    if (auto ssa = std::get_if<SSAValue>(v); ssa) {
                        used[ssa->def] = true;
                    }
                };
                for (auto& inst : block->insts()) {
                    for (auto& var : utils::vars(inst)) {
                        track(var);
                    }
                }
                if (auto var = utils::used_var(block->exit()); var) {
                    track(var);
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