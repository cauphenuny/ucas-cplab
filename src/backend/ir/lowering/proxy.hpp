/// @note Add a proxy for access to global variable / constexpr in ordered to assign it a register
/// because isel can only assign at most one temp register in i-sel stage.

#pragma once
#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "backend/ir/transform/framework.hpp"

#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ir::lowering {

template <typename T> struct AccessProxyLowering : transform::Pass<T> {
    bool apply(Program& prog, T& ctx) override {
        bool changed = false;
        auto globals = std::unordered_set<const Alloc*>{};
        for (auto& global : prog.globals()) {
            globals.insert(global.get());
        }
        for (auto& func : prog.funcs()) changed |= apply_func(*func, globals, ctx);
        return changed;
    }

private:
    bool apply_func(Func& func, const std::unordered_set<const Alloc*>& globals, T& ctx) {
        bool changed = false;
        for (auto& block : func.blocks()) {
            changed |= convert_global(func, *block, globals, ctx);
            changed |= convert_constexpr(func, *block);
        }
        return changed;
    }

    bool convert_global(Func& func, Block& block, const std::unordered_set<const Alloc*>& globals,
                        T& ctx) {
        using namespace analysis;
        bool changed = false;
        std::unordered_map<const Alloc*, std::vector<std::pair<Type, LeftValue>>> proxy_map;
        for (auto it = block.insts().begin(); it != block.insts().end(); ++it) {
            auto inst = *it;
            if (auto* unary = std::get_if<UnaryInst>(&inst)) {
                if (unary->op == UnaryInstOp::MOV || unary->op == UnaryInstOp::CONVERT) continue;
            }
            bool inst_changed = false;
            for (auto var : utils::used_vars(inst)) {
                if (auto* nv = std::get_if<NamedValue>(var)) {
                    if (auto* alloc = std::get_if<const Alloc*>(&nv->def)) {
                        if (globals.count(*alloc) == 0) continue;  // not a global variable
                        // Found a use of a global variable. Replace it with a proxy temp-var.
                        if (!proxy_map.count(*alloc)) {
                            proxy_map[*alloc] = {};
                        }
                        auto& proxies = proxy_map[*alloc];
                        auto pit = std::find_if(proxies.begin(), proxies.end(),
                                                [&](const auto& p) { return p.first == nv->type; });
                        LeftValue proxy;
                        if (pit == proxies.end()) {
                            proxy = func.newTemp(nv->type, &block);
                            proxies.emplace_back(nv->type, proxy);
                            block.insert(it, UnaryInst{.op = UnaryInstOp::MOV,
                                                       .result = proxy,
                                                       .operand = *var});
                        } else {
                            proxy = pit->second;
                        }
                        *var = proxy;
                        inst_changed = true;
                    }
                }
            }
            if (inst_changed) {
                changed = true;
                block.replace(&(*it), inst);
            }
        }
        return changed;
    }

    bool convert_constexpr(Func& func, Block& block) {
        // only convert constexpr in STORE inst, because t0 might be used as addr,
        // and store-inst can not directly take a num operand
        bool changed = false;
        for (auto it = block.insts().begin(); it != block.insts().end(); ++it) {
            auto& inst = *it;
            if (auto binary = std::get_if<BinaryInst>(&inst);
                binary && binary->op == InstOp::STORE) {
                if (auto* cv = std::get_if<ConstexprValue>(&binary->rhs)) {
                    if (cv->type.is<type::Primitive>()) {
                        LeftValue proxy = func.newTemp(cv->type, &block);
                        block.insert(
                            it, UnaryInst{.op = UnaryInstOp::MOV, .result = proxy, .operand = *cv});
                        block.replace(&inst, BinaryInst{.op = InstOp::STORE,
                                                        .result = binary->result,
                                                        .lhs = binary->lhs,
                                                        .rhs = proxy});
                        changed = true;
                    }
                }
            }
        }
        return changed;
    }
};

}  // namespace ir::lowering