/// @brief Copy Propagation Pass, requires SSA

/// @note propagates through non-array MOV operation

#pragma once
#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "backend/ir/type.hpp"
#include "framework.hpp"

#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace ir::optim {

struct CopyPropagation : SSAPass {
    bool apply(Program& prog, SSAPassContext& ctx) override {
        if (!prog.is_ssa) {
            throw COMPILER_ERROR("CopyPropagation requires SSA form");
        }
        bool changed = false;
        for (auto& func : prog.funcs()) {
            while (propagate(*func, prog, ctx)) changed = true;
        }
        return changed;
    }

private:
    bool propagate(Func& func, Program& prog, SSAPassContext& ctx) {
        std::unordered_map<Value, Value> copies;

        for (auto& global : prog.globals()) {
            if (global->comptime && !global->type.is<type::Array>()) {
                copies[LeftValue{global->value()}] = *global->init;
            }
        }

        auto get_root = [&](auto self, Value v) -> Value {
            std::unordered_set<Value> visited;
            while (true) {
                auto it = copies.find(v);
                if (it == copies.end() || visited.count(it->second)) break;
                visited.insert(it->second);
                v = it->second;
            }
            return v;
        };

        /// Identify copies
        for (auto& block : func.blocks()) {
            for (auto& inst : block->insts()) {
                if (auto unary = std::get_if<UnaryInst>(&inst)) {
                    if (unary->op == UnaryInstOp::MOV) {
                        if (type_of(unary->operand).is<type::Array>()) continue;
                        copies[unary->result] = unary->operand;
                    }
                } else if (auto phi = std::get_if<PhiInst>(&inst); phi) {
                    std::optional<Value> uniform_val;
                    bool possible = true;
                    for (auto& [_, val] : phi->args) {
                        auto var = analysis::utils::as_var(val);
                        if (!var) {
                            possible = false;
                            break;
                        }
                        auto root = get_root(get_root, *var);
                        if (root == Value{phi->result}) continue;
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
        for (auto& [val, root] : copies) {
            auto ultimate_root = get_root(get_root, root);
            if (auto var = std::get_if<LeftValue>(&val)) {
                changed |= ctx.ud.replace_all_uses_with(*var, ultimate_root);
            }
        }

        return changed;
    }
};

}  // namespace ir::optim