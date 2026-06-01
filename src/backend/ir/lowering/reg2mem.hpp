/// @note Convert non-ref Alloc which cannot be put in register to ref Alloc
#pragma once

#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/transform/framework.hpp"

#include <unordered_map>
#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace ir::transform {

namespace {

inline void replace_instruments(Func& func, const std::unordered_set<LeftValue>& spill_set) {
    for (auto& block : func.blocks()) {
        for (auto it = block->insts().begin(); it != block->insts().end();) {
            bool hit = false;
            for (auto use : utils::used_vars(*it))
                if (spill_set.count(*use)) hit = true;
            if (auto def = utils::defined_var(*it); def && spill_set.count(*def)) hit = true;
            if (!hit) {
                ++it;
                continue;
            }
            Inst inst = *it;
            auto next_it = block->erase(it);
            std::unordered_map<LeftValue, LeftValue> load_cache;
            for (auto use : utils::used_vars(inst)) {
                if (spill_set.count(*use)) {
                    if (!load_cache.count(*use)) {
                        auto elem_type = type_of(*use).as<ir::type::Reference>().elem;
                        auto temp = LeftValue{func.newTemp(elem_type, block.get())};
                        block->insert(
                            next_it,
                            UnaryInst{.op = UnaryInstOp::LOAD, .result = temp, .operand = *use});
                        load_cache[*use] = temp;
                    }
                    *use = load_cache[*use];
                }
            }
            if (auto def = utils::defined_var(inst); def && spill_set.count(*def)) {
                auto elem_type = type_of(*def).as<ir::type::Reference>().elem;
                auto temp = LeftValue{func.newTemp(elem_type, block.get())};
                auto target = *def;
                *def = temp;
                block->insert(next_it, std::move(inst));
                block->insert(next_it, BinaryInst{.op = InstOp::STORE,
                                                  .result = std::nullopt,
                                                  .lhs = target,
                                                  .rhs = temp});
            } else {
                block->insert(next_it, std::move(inst));
            }
            it = next_it;
        }
        if (block->hasExit()) {
            auto& exit = block->exit();
            if (auto use = utils::used_var(exit); use && spill_set.count(*use)) {
                auto elem_type = type_of(*use).as<ir::type::Reference>().elem;
                auto temp = LeftValue{func.newTemp(elem_type, block.get())};
                block->insert(block->insts().end(),
                              UnaryInst{.op = UnaryInstOp::LOAD, .result = temp, .operand = *use});
                *use = temp;
            }
        }
    }
}

}  // namespace

template <typename Context>
void spill(const std::unordered_set<Alloc*>& variables, std::variant<Func*, Program*> scope,
           Context& ctx) {
    std::unordered_set<LeftValue> spill_set;
    for (auto alloc : variables) {
        auto prev = alloc->value();
        alloc->reference = true;
        auto after = LeftValue{alloc->value()};
        ctx.ud.replace_all_defs_with(prev, after);
        ctx.ud.replace_all_uses_with(prev, after);
        spill_set.insert(after);
    }
    if (auto func = std::get_if<Func*>(&scope)) {
        replace_instruments(**func, spill_set);
    } else if (auto prog = std::get_if<Program*>(&scope)) {
        for (auto& func : (*prog)->funcs()) {
            replace_instruments(*func, spill_set);
        }
    }
}

struct RegToMem : NonSSAPass {
    using TargetABI = ir::lowering::TargetABI;
    RegToMem(TargetABI abi) : abi(std::move(abi)) {}

    bool apply(Program& prog, NonSSAPassContext& ctx) override {
        using namespace type;
        std::unordered_set<Alloc*> workset;
        for (auto& global : prog.globals()) {
            if (!global->reference) workset.insert(global.get());
        }
        for (auto& func : prog.funcs()) {
            auto param_regs = assign_param_regs(func->params, abi);
            for (size_t i = 0; i < func->params.size(); i++) {
                if (param_regs[i]) continue;
                workset.insert(func->params[i].get());
            }
        }
        if (workset.empty()) return false;
        spill(workset, &prog, ctx);
        return true;
    }

    TargetABI abi;
};

}  // namespace ir::transform
