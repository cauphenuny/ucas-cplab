/// @brief Dead Block Elimination Pass, must run after SSA construction

#pragma once

#include "backend/ir/analysis/cfg.hpp"
#include "backend/ir/ir.hpp"
#include "framework.hpp"

#include <algorithm>
#include <unordered_set>
#include <variant>
#include <vector>

namespace ir::optim {

/// @note eliminate unreachable blocks.

struct DeadBlockElimination : Pass {
    bool apply(Program& prog) override {
        bool pass_changed = false;

        for (auto& func_ptr : prog.getFuncs()) {
            ir::Func& func = *func_ptr;

            std::unordered_set<Block*> reachable;
            std::vector<Block*> worklist;
            Block* entry = func.entrance();

            worklist.push_back(entry);
            reachable.insert(entry);

            while (!worklist.empty()) {
                Block* curr = worklist.back();
                worklist.pop_back();

                if (curr->hasExit()) {
                    match(
                        curr->exit(), [](const ReturnExit&) {},
                        [&](const JumpExit& j) {
                            if (reachable.insert(j.target).second) {
                                worklist.push_back(j.target);
                            }
                        },
                        [&](const BranchExit& br) {
                            if (reachable.insert(br.true_target).second) {
                                worklist.push_back(br.true_target);
                            }
                            if (reachable.insert(br.false_target).second) {
                                worklist.push_back(br.false_target);
                            }
                        });
                }
            }

            auto& blocks = func.blocks();
            auto it = std::remove_if(blocks.begin(), blocks.end(),
                                     [&](const std::unique_ptr<Block>& blk) {
                                         return reachable.find(blk.get()) == reachable.end();
                                     });

            if (it != blocks.end()) {
                blocks.erase(it, blocks.end());
                pass_changed = true;
            }
        }
        return pass_changed;
    }
};

/// @note: replace block with only a jump exit by its target

struct TrivialBlockReplacement : Pass {
    bool apply(Program& prog) override {
        bool pass_changed = false;
        for (auto& func_box : prog.getFuncs()) {
            pass_changed |= replace(*func_box, prog);
        }
        return pass_changed;
    }

private:
    // replace block by its predecessors in phi
    void refine_phi(PhiInst& inst, Block* replaced, ControlFlowGraph& cfg) {
        std::unordered_map<Block*, Value> new_args;
        for (auto& [block, arg] : inst.args) {
            if (block == replaced) {
                for (auto pred : cfg.pred[block]) {
                    new_args[pred] = arg;
                }
            } else {
                new_args[block] = arg;
            }
        }
        inst.args = std::move(new_args);
    }

    // replace block by its target in exit
    bool replace(Func& func, Program& prog) {
        auto cfg = ControlFlowGraph(func);
        std::optional<std::pair<Block*, Block*>> replacement;
        for (auto& block : func.blocks()) {
            if (block.get() == func.entrance()) continue;
            if (block->insts().size() == 0) {
                match(
                    block->exit(),
                    [&](const JumpExit& j) { replacement = {block.get(), j.target}; },
                    [](const auto&) {});
            }
            if (replacement.has_value()) break;
        }

        if (!replacement) return false;

        auto replaced = replacement->first, target = replacement->second;

        for (auto& block : func.blocks()) {
            for (auto& inst : block->insts()) {
                if (auto phi = std::get_if<PhiInst>(&inst); phi) {
                    refine_phi(*phi, replaced, cfg);
                } else {
                    break;
                }
            }

            match(
                block->exit(),
                [&](JumpExit& j) {
                    if (j.target == replaced) {
                        j.target = target;
                    }
                },
                [&](BranchExit& b) {
                    if (b.true_target == replaced) {
                        b.true_target = target;
                    }
                    if (b.false_target == replaced) {
                        b.false_target = target;
                    }
                },
                [](ReturnExit&) {});
        }

        return true;
    }

    bool squash(Func& func, Program& prog) {
        if (func.entrance()->insts().size() > 0 ||
            !std::holds_alternative<JumpExit>(func.entrance()->exit()))
            return false;
        auto target = std::get<JumpExit>(func.entrance()->exit()).target;
        for (auto& block : func.blocks()) {
            if (block.get() == target) {
                std::swap(func.blocks()[0], block);
                std::swap(block, func.blocks().back());
                func.blocks().pop_back();
            }
        }
        return true;
    }
};

struct TrivialBlockElimination : Pass {
    bool apply(Program& prog) override {
        bool changed = false;
        while (run(prog)) {
            changed = true;
            fmt::println("---------\n{}", prog);
        }
        return changed;
    }

private:
    bool run(Program& prog) {
        bool changed = false;
        changed |= TrivialBlockReplacement().apply(prog);
        changed |= DeadBlockElimination().apply(prog);
        return changed;
    }
};

}  // namespace ir::optim