/// @brief Copy Propagation Pass, must run after SSA construction

#pragma once
#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.hpp"
#include "framework.hpp"

#include <unordered_map>

namespace ir::optim {

struct CopyPropagation : Pass {
    bool apply(Program& prog) override {
        bool changed = false;
        for (auto& func : prog.getFuncs()) {
            while (propagate(*func)) changed = true;
        }
        return changed;
    }

private:
    bool propagate(Func& func) {
        std::unordered_map<LeftValue, LeftValue> copies;

        auto get_root = [&](auto self, LeftValue v) -> LeftValue {
            auto it = copies.find(v);
            if (it != copies.end()) {
                return it->second = self(self, it->second);
            }
            return v;
        };

        /// Identify copies
        for (auto& block : func.blocks()) {
            for (auto& inst : block->insts()) {
                if (auto unary = std::get_if<UnaryInst>(&inst)) {
                    if (unary->op == UnaryInstOp::MOV) {
                        if (auto operand = analysis::utils::as_var(unary->operand)) {
                            if (!(*operand == unary->result)) {
                                copies[unary->result] = *operand;
                            }
                        }
                    }
                } else if (auto phi = std::get_if<PhiInst>(&inst)) {
                    std::optional<LeftValue> uniform_val;
                    bool possible = true;
                    for (auto& [_, val] : phi->args) {
                        auto var = analysis::utils::as_var(val);
                        if (!var) {
                            possible = false;
                            break;
                        }
                        auto root = get_root(get_root, *var);
                        if (root == phi->result) continue;  // ignore self-loops in Phi
                        if (!uniform_val) {
                            uniform_val = root;
                        } else if (!(*uniform_val == root)) {
                            possible = false;
                            break;
                        }
                    }
                    if (possible && uniform_val) {
                        copies[phi->result] = *uniform_val;
                    }
                }
            }
        }

        if (copies.empty()) return false;

        /// Perform replacement
        bool changed = false;
        for (auto& block : func.blocks()) {
            for (auto& inst : block->insts()) {
                for (auto use : analysis::utils::used_vars(inst)) {
                    auto root = get_root(get_root, *use);
                    if (!(*use == root)) {
                        *use = root;
                        changed = true;
                    }
                }
            }
            if (auto exit_use = analysis::utils::used_var(block->exit())) {
                auto root = get_root(get_root, *exit_use);
                if (!(*exit_use == root)) {
                    *exit_use = root;
                    changed = true;
                }
            }
        }

        return changed;
    }
};

}  // namespace ir::optim