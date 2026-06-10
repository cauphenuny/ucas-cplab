/// @note Lower directly access to global variable to a proxy temp-var in ordered to assign it a
/// register

#pragma once
#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "backend/ir/transform/framework.hpp"

#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ir::lowering {

template <typename T> struct GlobalProxyLowering : transform::Pass<T> {
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
        for (auto& block : func.blocks()) changed |= apply_block(func, *block, globals, ctx);
        return changed;
    }

    bool apply_block(Func& func, Block& block, const std::unordered_set<const Alloc*>& globals,
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
};

}  // namespace ir::lowering