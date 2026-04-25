/// @brief Dead Allocation Elimination Pass

#pragma once

#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "framework.hpp"

#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ir::optim {
struct DeadAllocElimination : SSAPass {
    bool apply(Program& prog, SSAPassContext& ctx) override {
        if (!prog.is_ssa) {
            throw COMPILER_ERROR("DeadAllocElimination requires SSA form");
        }
        bool changed = false;
        std::unordered_map<const Alloc*, bool> referenced;
        for (auto& func : prog.funcs()) {
            for (auto var : analysis::utils::vars(*func)) {
                if (auto named = std::get_if<NamedValue>(var); named) {
                    if (auto alloc = std::get_if<const Alloc*>(&named->def); alloc) {
                        referenced[*alloc] = true;
                    }
                }
                if (auto ssa = std::get_if<SSAValue>(var); ssa) {
                    referenced[ssa->def] = true;
                }
            }
            std::vector<std::unique_ptr<Alloc>> kept_locals;
            for (auto& alloc : func->locals()) {
                if (referenced.count(alloc.get())) {
                    kept_locals.push_back(std::move(alloc));
                } else {
                    changed = true;
                }
            }
            func->locals() = std::move(kept_locals);
        }
        std::vector<std::unique_ptr<Alloc>> kept_globals;
        for (auto& global : prog.globals()) {
            if (referenced.count(global.get())) {
                kept_globals.push_back(std::move(global));
            } else {
                changed = true;
            }
        }
        prog.globals() = std::move(kept_globals);
        return changed;
    }
};

}  // namespace ir::optim