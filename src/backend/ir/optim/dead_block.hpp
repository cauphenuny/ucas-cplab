/// @brief CFG Simplification & Dead Block Elimination Pass, requires SSA

#pragma once

#include "backend/ir/analysis/cfg.hpp"
#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "framework.hpp"

#include <algorithm>
#include <list>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace ir::optim {

/// @note eliminate unreachable blocks.

struct DeadBlockElimination : Pass {
    bool apply(Program& prog) override {
        if (!prog.is_ssa) {
            throw COMPILER_ERROR("DeadBlockElimination requires SSA form");
        }
        bool pass_changed = false;
        while (eliminate(prog)) pass_changed = true;
        return pass_changed;
    }

private:
    bool eliminate(Program& prog) {
        bool pass_changed = false;

        for (auto& func_ptr : prog.funcs()) {
            ir::Func& func = *func_ptr;

            std::unordered_set<Block*> reachable;
            std::vector<Block*> worklist;
            Block* entry = func.entrance();

            worklist.push_back(entry);
            reachable.insert(entry);

            while (!worklist.empty()) {
                Block* curr = worklist.back();
                worklist.pop_back();

                for (auto target_ref : analysis::utils::targets(curr->exit())) {
                    auto target = target_ref.get();
                    if (reachable.insert(target).second) {
                        worklist.push_back(target);
                    }
                }
            }

            auto& blocks = func.blocks();

            pass_changed |= reachable.size() != blocks.size();

            for (auto& block_box : blocks) {
                Block* blk = block_box.get();
                if (reachable.find(blk) == reachable.end()) continue;

                for (auto& inst : blk->insts()) {
                    if (auto phi = std::get_if<PhiInst>(&inst); phi) {
                        auto [changed, new_phi] = refine_phi(*phi, reachable);
                        if (changed) {
                            blk->replace(&inst, new_phi);
                        }
                    } else {
                        break;
                    }
                }
            }

            auto it = std::remove_if(blocks.begin(), blocks.end(),
                                     [&](const std::unique_ptr<Block>& blk) {
                                         return reachable.find(blk.get()) == reachable.end();
                                     });

            while (it != blocks.end()) {
                it = func.removeBlock(it);
            }
        }
        return pass_changed;
    }

    std::pair<bool, PhiInst> refine_phi(PhiInst phi, const std::unordered_set<Block*>& reachable) {
        bool changed = false;
        for (auto it = phi.args.begin(); it != phi.args.end();) {
            if (reachable.find(it->first) == reachable.end()) {
                it = phi.args.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        return {changed, phi};
    }
};

/// @note:
/// 1. replace block with only a jump exit by its target
/// 2. redirect empty entrance to its target

struct SimplifyCFG : Pass {
    bool apply(Program& prog) override {
        bool changed = false;
        while (replace(prog)) changed = true;
        return changed;
    }

private:
    bool replace(Program& prog) {
        bool pass_changed = false;
        for (auto& func_box : prog.funcs()) {
            pass_changed |= squash(*func_box, prog);
            pass_changed |= redirect(*func_box, prog);
        }
        return pass_changed;
    }
    // replace block by its predecessors in phi
    void refine_phi(PhiInst& inst, Block* replaced, ControlFlowGraph& cfg) {
        std::vector<std::pair<Block*, Value>> new_args;
        std::unordered_set<Block*> preds;
        for (auto& [block, arg] : inst.args) {
            if (block != replaced) {
                new_args.emplace_back(block, arg);
                preds.insert(block);
            }
        }
        for (auto& [block, arg] : inst.args) {
            if (block == replaced) {
                for (auto pred : cfg.pred[block]) {
                    if (preds.count(pred)) {
                        throw COMPILER_ERROR(
                            fmt::format("conflict phi args, caused by replacng {} to {} in {}",
                                        block->label, pred->label, inst));
                    }
                    new_args.emplace_back(pred, arg);
                    preds.insert(pred);
                }
            }
        }
        inst.args = std::move(new_args);
    }

    bool conflicts(Block* replaced, Block* target, ControlFlowGraph& cfg) {
        // Check: would any predecessor of `replaced` conflict with
        // an existing phi source in `target`?
        for (auto& inst : target->insts()) {
            if (auto phi = std::get_if<PhiInst>(&inst); phi) {
                for (auto pred : cfg.pred[replaced]) {
                    if (phi->contains(pred)) return true;
                }
            } else {
                break;
            }
        }
        return false;
    }

    bool mergable(Block* from, Block* to) {
        if (from->insts().empty() || to->insts().empty()) return true;
        if (std::holds_alternative<PhiInst>(to->insts().front()) && !from->insts().empty())
            return false;
        return true;
    }

    void merge(Block* from, Block* to) {
        std::list<Inst> merged_insts;
        merged_insts.splice(merged_insts.end(), from->insts());
        merged_insts.splice(merged_insts.end(), to->insts());
        to->insts() = std::move(merged_insts);
    }

    // merge block to its target in jump-exit
    bool squash(Func& func, Program& prog) {
        auto cfg = ControlFlowGraph(func);
        std::optional<std::pair<Block*, Block*>> replacement;
        for (auto& block_box : func.blocks()) {
            auto block = block_box.get();
            if (cfg.pred[block].empty()) {
                // no predecessor, would not modify any phi inst or any exit inst, so skip it
                continue;
            }
            match(
                block->exit(),
                [&](const JumpExit& j) {
                    if (block == j.target) return;
                    if (block->insts().size() == 0) {
                        if (!conflicts(block, j.target, cfg)) {
                            replacement = {block, j.target};
                        }
                    } else if (cfg.pred[j.target].size() == 1) {
                        // block has inst, can not be rearranged after target's phi inst, so target
                        // must not have phi inst
                        if (mergable(block, j.target)) {
                            replacement = {block, j.target};
                        }
                    }
                },
                [](const auto&) {});
            if (replacement.has_value()) break;
        }

        if (!replacement) return false;

        auto replaced = replacement->first, target = replacement->second;

        merge(replaced, target);

        for (auto& block : func.blocks()) {
            for (auto& inst : block->insts()) {
                if (auto phi = std::get_if<PhiInst>(&inst); phi) {
                    refine_phi(*phi, replaced, cfg);
                } else {
                    break;
                }
            }

            for (auto ref : analysis::utils::targets(block->exit())) {
                if (ref.get() == replaced) {
                    ref.get() = target;
                }
            }
        }

        return true;
    }

    // redirect entrance to its target if possible
    bool redirect(Func& func, Program& prog) {
        if (!std::holds_alternative<JumpExit>(func.entrance()->exit())) return false;
        auto target = std::get<JumpExit>(func.entrance()->exit()).target;

        if (!mergable(func.entrance(), target)) return false;
        // entry block has no predecessor, so phi instructions cannot be resolved
        if (!target->insts().empty() && std::holds_alternative<PhiInst>(target->insts().front()))
            return false;
        auto cfg = ControlFlowGraph(func);
        if (cfg.pred[target].size() > 1) return false;
        merge(func.entrance(), target);

        for (auto& block : func.blocks()) {
            if (block.get() == target) {
                std::swap(func.blocks()[0]->label, block->label);
                std::swap(func.blocks()[0], block);
                std::swap(block, func.blocks().back());
            }
        }
        return true;
    }
};

}  // namespace ir::optim