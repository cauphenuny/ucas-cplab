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
        std::unordered_map<Value, Value> copies;

        auto get_root = [&](auto self, Value v) -> Value {
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
                        copies[unary->result] = unary->operand;
                    }
                } else if (auto phi = std::get_if<PhiInst>(&inst)) {
                    std::optional<Value> uniform_val;
                    bool possible = true;
                    for (auto& [_, val] : phi->args) {
                        auto var = analysis::utils::as_var(val);
                        if (!var) {
                            possible = false;
                            break;
                        }
                        auto root = get_root(get_root, *var);
                        if (root == Value{phi->result}) continue;  // ignore self-loops in Phi
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
                for (auto use : analysis::utils::uses(inst)) {
                    match(
                        use,
                        [&](Value* v) {
                            auto root = get_root(get_root, *v);
                            if (!(*v == root)) {
                                *v = root;
                                changed = true;
                            }
                        },
                        [&](LeftValue* v) {
                            auto root = get_root(get_root, *v);
                            auto lval = analysis::utils::as_var(root);
                            if (lval) {
                                if (!(*v == *lval)) {
                                    *v = *lval;
                                    changed = true;
                                }
                            } else {
                                throw COMPILER_ERROR(
                                    fmt::format("Copy propagation propagates constexpr value to "
                                                "lval-only location: {}",
                                                root));
                            }
                        });
                }
            }
            if (auto exit_use = analysis::utils::used(block->exit())) {
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