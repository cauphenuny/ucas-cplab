/// @brief Pass Constructing SSA IR

#pragma once

#include "backend/ir/analysis/cfg.hpp"
#include "backend/ir/analysis/dataflow/dominance.hpp"
#include "backend/ir/analysis/dataflow/framework.hpp"
#include "backend/ir/analysis/dataflow/liveness.hpp"
#include "backend/ir/analysis/dominance.hpp"
#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.hpp"
#include "framework.hpp"

#include <memory>
#include <queue>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace ir::optim {

namespace ssa_impl {

struct AddPhi : Pass {
    bool apply(Program& prog) override {
        bool changed = false;
        for (auto& func : prog.getFuncs()) {
            changed |= transform(*func, prog);
        }
        return changed;
    }

private:
    auto definitions(const Func& func, const Alloc* alloc) {
        std::unordered_set<Block*> defs;
        auto val = LeftValue{alloc->value()};

        for (const auto& block_box : func.blocks()) {
            auto block = block_box.get();
            for (auto& inst : block->insts()) {
                if (auto def = utils::defined_var(inst); def && *def == val) {
                    defs.insert(block);
                }
            }
        }
        return defs;
    }

    bool transform(Func& func, Program& prog) {
        auto cfg = ControlFlowGraph(func);
        auto dom_flow = DataFlow<flow::Dominance>(cfg, prog);
        auto dom_tree = DominanceTree(dom_flow);
        auto dom_frontier = DominanceFrontier(cfg, dom_tree);
        auto live_vars = DataFlow<flow::Liveness>(cfg, prog);

        auto allocs = std::unordered_set<Alloc*>();
        for (auto& alloc : func.locals()) allocs.insert(alloc.get());
        for (auto& alloc : func.params) allocs.insert(alloc.get());

        bool changed = false;
        for (auto& alloc : allocs) {
            if (alloc->immutable) continue;  // immutable value does not need phi
            changed = true;
            auto in_worklist = definitions(func, alloc);
            std::queue<Block*> worklist;
            for (auto block : in_worklist) worklist.push(block);
            std::unordered_map<Block*, bool> has_phi;
            std::unordered_map<Block*, bool> visited;

            auto val = alloc->value();

            while (worklist.size()) {
                auto block = worklist.front();
                worklist.pop();
                if (visited[block]) continue;
                visited[block] = true;
                for (auto frontier : dom_frontier.frontier(block)) {
                    if (!live_vars.in[frontier].contains(val))
                        continue;  // no need to insert phi if not live-in
                    if (!has_phi[frontier]) {
                        auto phi = PhiInst{.result = val};
                        for (auto pred : cfg.pred[frontier]) {
                            phi.args[pred] = LeftValue{val};
                        }
                        frontier->prepend(Inst{std::move(phi)});
                        has_phi[frontier] = true;
                    }
                    if (!in_worklist.count(frontier)) {
                        worklist.push(frontier);
                        in_worklist.insert(frontier);
                    }
                }
            }
        }
        return changed;
    }
};

struct Rename : Pass {
    bool apply(Program& prog) override {
        bool changed = false;
        for (auto& func : prog.getFuncs()) {
            changed |= transform(*func, prog);
        }
        return changed;
    }

private:
    std::unordered_set<const Alloc*> need_rename;
    std::unordered_map<const Alloc*, size_t> version;
    std::unordered_map<const Alloc*, std::stack<SSAValue>> rename_stack;
    std::unique_ptr<ControlFlowGraph> cfg;
    std::unique_ptr<DataFlow<flow::Dominance>> dom_flow;
    std::unique_ptr<DominanceTree> dom_tree;

    void rename(Block& block, const Func& func) {
        // fmt::println(stderr, "Renaming block {}", block.label);
        std::unordered_map<const Alloc*, size_t> push_count;
        auto rename_var = [&](LeftValue& v) {
            auto alloc = utils::alloc_of(v);
            if (alloc && need_rename.count(alloc)) {
                if (rename_stack[alloc].size()) {
                    v = rename_stack[alloc].top();
                }
            }
        };
        for (auto& inst : block.insts()) {
            // 1. Rename uses
            if (!std::holds_alternative<PhiInst>(inst)) {
                for (auto use : utils::used_vars(inst)) rename_var(*use);
            }

            // 2. Rename definition
            auto def = utils::defined_var(inst);
            if (def) {
                auto alloc = utils::alloc_of(*def);
                if (alloc && need_rename.count(alloc)) {
                    auto ssa_value = SSAValue(type_of(*def), alloc, version[alloc]++);
                    *def = ssa_value;
                    // fmt::println(stderr, "push {}", ssa_value);
                    rename_stack[alloc].push(ssa_value);
                    push_count[alloc]++;
                }
            }
        }
        // 3. Rename uses in Exit
        if (auto var = utils::used_var(block.exit()); var) {
            rename_var(*var);
        }
        for (auto& succ : cfg->succ[&block]) {
            for (auto& inst : succ->insts()) {
                if (auto phi = std::get_if<PhiInst>(&inst)) {
                    for (auto& [pred, val] : phi->args) {
                        if (pred == &block) {
                            if (auto var = utils::as_var(val); var) {
                                rename_var(*var);
                            }
                        }
                    }
                } else {
                    break;
                }
            }
        }
        for (auto& child : dom_tree->children(&block)) {
            rename(*child, func);
        }
        for (auto& [alloc, count] : push_count) {
            for (size_t i = 0; i < count; i++) {
                rename_stack[alloc].pop();
            }
        }
    }

    bool transform(Func& func, Program& program) {
        need_rename.clear();
        version.clear();
        rename_stack.clear();
        cfg = std::make_unique<ControlFlowGraph>(func);
        dom_flow = std::make_unique<DataFlow<flow::Dominance>>(*cfg, program);
        dom_tree = std::make_unique<DominanceTree>(*dom_flow);

        auto allocs = std::unordered_set<Alloc*>();
        for (auto& alloc : func.locals()) allocs.insert(alloc.get());
        for (auto& alloc : func.params) allocs.insert(alloc.get());

        for (const auto& alloc : allocs) {
            if (!alloc->immutable) {
                need_rename.insert(alloc);
            }
        }
        if (need_rename.empty()) return false;
        rename(*func.entrance(), func);
        for (const auto& alloc : allocs) {
            alloc->immutable = true;  // after renaming, all allocs can be treated as immutable
        }
        return true;
    }
};

}  // namespace ssa_impl

using ToSSA = Compose<ssa_impl::AddPhi, ssa_impl::Rename>;

struct SSAValue2TempValue : Pass {
    bool apply(Program& prog) override {
        bool changed = false;
        for (auto& func : prog.getFuncs()) {
            std::unordered_map<SSAValue, TempValue> ssa_to_temp;
            for (auto& block : func->blocks()) {
                auto convert = [&](LeftValue* v) {
                    if (auto ssa = std::get_if<SSAValue>(v); ssa) {
                        if (!ssa_to_temp.count(*ssa)) {
                            ssa_to_temp[*ssa] = func->newTemp(ssa->type, block.get());
                            changed = true;
                        }
                        *v = ssa_to_temp[*ssa];
                    }
                };
                for (auto& inst : block->insts()) {
                    for (auto& var : utils::vars(inst)) {
                        convert(var);
                    }
                }
                if (auto var = utils::used_var(block->exit()); var) {
                    convert(var);
                }
            }
        }
        return changed;
    }
};

}  // namespace ir::optim