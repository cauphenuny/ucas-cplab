/// @brief Exit from SSA Form by eliminating phi instructions

/// @note algorithm from Practical Improvements to the Construction and Destruction of Static Single
/// Assignment Form, Briggs et al. 1998
/// slightly modifyed by add a split-critical-edge pass to handle so-called "lost copy" problem.

#include "backend/ir/analysis/cfg.hpp"
#include "backend/ir/analysis/dominance.hpp"
#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "framework.hpp"
#include "utils/diagnosis.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace ir::optim {

namespace minipass {

struct SplitCriticalEdge : NonSSAPass {
    bool apply(Program& prog, NonSSAPassContext& ctx) override {
        bool changed = false;
        for (auto& func : prog.funcs()) {
            changed |= split(*func);
        }
        return changed;
    }

private:
    bool split(Func& func) {
        auto cfg = ControlFlowGraph(func);
        struct Edge {
            Block* from;
            Block* to;
        };
        auto critical_edges = std::vector<Edge>();
        for (auto& blk : func.blocks()) {
            auto& preds = cfg.pred[blk.get()];
            if (preds.size() <= 1) continue;
            if (!blk->insts().size() || !std::holds_alternative<PhiInst>(blk->insts().front()))
                continue;
            for (auto pred : preds) {
                if (cfg.succ[pred].size() > 1) {
                    critical_edges.push_back({pred, blk.get()});
                }
            }
        }
        if (!critical_edges.size()) return false;
        for (auto edge : critical_edges) {
            auto mid = func.newBlock();
            mid->setExit(JumpExit{edge.to});
            for (auto target : utils::targets(edge.from->exit())) {
                if (target.get() == edge.to) target.get() = mid;
            }
            for (auto& inst : edge.to->insts()) {
                if (!std::holds_alternative<PhiInst>(inst)) break;
                for (auto source : utils::sources(inst)) {
                    if (source.get() == edge.from) source.get() = mid;
                }
            }
        }
        return true;
    }
};

struct ReplacePhi : NonSSAPass {
    bool apply(Program& prog, NonSSAPassContext& ctx) override {
        bool changed = false;
        for (auto& func : prog.funcs()) {
            changed |= replace_phi(*func, prog);
        }
        legalize(prog, ctx);
        return changed;
    }

private:
    void legalize(Program& prog, NonSSAPassContext& ctx) {
        std::unordered_set<LeftValue> illegals;
        for (auto& [var, sites] : ctx.ud.all_defs()) {
            if (sites.size() > 1 && !std::holds_alternative<NamedValue>(var)) {
                illegals.insert(var);
            }
        }

        struct Info {
            std::string name;
            Func* scope;
        };
        auto where = [&](const Alloc* alloc) -> Func* {
            for (auto& func : prog.funcs()) {
                for (auto& local : func->locals()) {
                    if (local.get() == alloc) return func.get();
                }
                for (auto& param : func->params) {
                    if (param.get() == alloc) return func.get();
                }
            }
            throw COMPILER_ERROR(fmt::format("alloc {} not found in any function", alloc->name));
        };

        for (auto var : illegals) {
            auto [name, scope] = Match{var}(
                [&](const TempValue& temp) -> Info {
                    return {fmt::format("_{}", temp.id), temp.func};
                },
                [&](const SSAValue& ssa) -> Info {
                    return {fmt::format("_{}_{}", ssa.def->name, ssa.version), where(ssa.def)};
                },
                [&](const NamedValue& named) -> Info {
                    throw COMPILER_ERROR(
                        "detected NamedValue in illegal single-assignment variable");
                });
            auto alloc = Alloc::variable(name, type_of(var));
            auto alloc_var = LeftValue{alloc->value()};
            ctx.ud.replace_all_uses_with(var, alloc_var);
            ctx.ud.replace_all_defs_with(var, alloc_var);
            scope->addLocal(std::move(alloc));
        }
    }

    bool replace_phi(Func& func, Program& prog) {
        auto cfg = ControlFlowGraph(func);
        auto domflow = analysis::DataFlow<analysis::flow::Dominance>(cfg, prog);
        auto domtree = analysis::DominanceTree(domflow);
        insert_copy(*func.entrance(), func, cfg, domtree);
        bool changed = false;
        for (auto& block : func.blocks()) {
            changed |= remove_phi(*block);
        }
        return changed;
    }

    auto schedule_copy(Block& block, Func& func, ControlFlowGraph& cfg) {
        /// pass 1: initialize the data structures
        auto copy_set = std::unordered_set<std::pair<Value, Value>>();  // {from, to}
        auto worklist = std::vector<std::pair<Value, Value>>();         // {from, to}
        auto map = std::unordered_map<Value, Value>();
        auto used = std::unordered_map<Value, bool>();

        for (auto succ : cfg.succ[&block]) {
            for (auto& inst : succ->insts()) {
                if (!std::holds_alternative<PhiInst>(inst)) break;
                auto& phi = std::get<PhiInst>(inst);
                auto src = phi.value(&block);
                auto dest = phi.result;
                copy_set.emplace(src, dest);
                map[src] = src;
                map[dest] = dest;
                used[src] = true;
            }
        }

        /// pass 2: set up the worklist of initial copies
        for (auto& [src, dest] : copy_set) {
            if (!used[dest]) {
                worklist.emplace_back(src, dest);
            }
        }
        for (auto& copy : worklist) {
            auto it = copy_set.find(copy);
            copy_set.erase(it);
        }

        /// pass 3: iterate over the worklist, inserting copies
        while (worklist.size() || copy_set.size()) {
            while (worklist.size()) {
                auto [src, dest] = worklist.back();
                worklist.pop_back();
                block.add(UnaryInst{
                    .op = UnaryInstOp::MOV, .result = as_lvalue(dest), .operand = map[src]});
                map[src] = dest;
                for (auto it = copy_set.begin(); it != copy_set.end();) {
                    auto& [s, d] = *it;
                    if (src == d) {
                        worklist.emplace_back(s, d);
                        it = copy_set.erase(it);
                    } else {
                        ++it;
                    }
                }
            }

            if (copy_set.size()) {
                auto it = copy_set.begin();
                auto [src, dest] = *it;
                copy_set.erase(it);

                auto temp = func.newTemp(type_of(dest), &block);
                block.add(UnaryInst{.op = UnaryInstOp::MOV, .result = temp, .operand = dest});
                map[dest] = Value{LeftValue{temp}};
                worklist.emplace_back(src, dest);
            }
        }
    }

    void insert_copy(Block& block, Func& func, ControlFlowGraph& cfg,
                     analysis::DominanceTree& domtree) {
        schedule_copy(block, func, cfg);
        for (auto child : domtree.children(&block)) {
            insert_copy(*child, func, cfg, domtree);
        }
    }

    bool remove_phi(Block& block) {
        bool changed = false;
        for (auto it = block.insts().begin(); it != block.insts().end();) {
            if (std::holds_alternative<PhiInst>(*it)) {
                it = block.erase(it);
                changed = true;
            } else {
                break;
            }
        }
        return changed;
    }
};

}  // namespace minipass

using DestructSSA = Compose<NonSSAPassContext, minipass::SplitCriticalEdge, minipass::ReplacePhi>;

}  // namespace ir::optim
