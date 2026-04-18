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
        while (eliminate(prog)) pass_changed = true;
        return pass_changed;
    }

private:
    bool eliminate(Program& prog) {
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

            auto& blocks = func.blocks();

            for (auto& block_box : blocks) {
                Block* blk = block_box.get();
                if (reachable.find(blk) == reachable.end()) continue;

                for (auto& inst : blk->insts()) {
                    if (auto phi = std::get_if<PhiInst>(&inst); phi) {
                        pass_changed |= refine_phi(*phi, reachable);
                    } else {
                        break;
                    }
                }
            }

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

    bool refine_phi(PhiInst& phi, const std::unordered_set<Block*>& reachable) {
        bool changed = false;
        for (auto it = phi.args.begin(); it != phi.args.end();) {
            if (reachable.find(it->first) == reachable.end()) {
                it = phi.args.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        return changed;
    }
};

/// @note: replace block with only a jump exit by its target

struct TrivialBlockReplacement : Pass {
    bool apply(Program& prog) override {
        bool changed = false;
        while (replace(prog)) changed = true;
        return changed;
    }

private:
    bool replace(Program& prog) {
        bool pass_changed = false;
        for (auto& func_box : prog.getFuncs()) {
            pass_changed |= replace(*func_box, prog);
            pass_changed |= squash(*func_box, prog);
        }
        return pass_changed;
    }
    // replace block by its predecessors in phi
    void refine_phi(PhiInst& inst, Block* replaced, ControlFlowGraph& cfg) {
        std::unordered_map<Block*, Value> new_args;
        for (auto& [block, arg] : inst.args) {
            if (block != replaced) {
                new_args[block] = arg;
            }
        }
        for (auto& [block, arg] : inst.args) {
            if (block == replaced) {
                for (auto pred : cfg.pred[block]) {
                    if (new_args.count(pred)) {
                        throw COMPILER_ERROR(
                            fmt::format("conflict phi args, caused by replacng {} to {} in {}",
                                        block->label, pred->label, inst));
                    }
                    new_args[pred] = arg;
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
                    if (phi->args.count(pred)) return true;
                }
            } else {
                break;
            }
        }
        return false;
    }

    // replace block by its target in exit
    bool replace(Func& func, Program& prog) {
        auto cfg = ControlFlowGraph(func);
        std::optional<std::pair<Block*, Block*>> replacement;
        for (auto& block_box : func.blocks()) {
            auto block = block_box.get();
            if (cfg.pred[block].empty()) {
                // fmt::println(stderr, "{} has no predecessor", block->label);
                continue;
            }
            if (block->insts().size() == 0) {
                match(
                    block->exit(),
                    [&](const JumpExit& j) {
                        if (block == j.target) return;
                        if (!conflicts(block, j.target, cfg)) {
                            replacement = {block, j.target};
                        }
                    },
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

        if (target->insts().size() && std::holds_alternative<PhiInst>(target->insts().front()))
            return false;
        auto cfg = ControlFlowGraph(func);
        if (cfg.pred[target].size() > 1) return false;

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

struct TrivialBlockElimination : Pass {
    bool apply(Program& prog) override {
        bool changed = false;
        while (run(prog)) {
            changed = true;
        }
        return changed;
    }

private:
    bool run(Program& prog) {
        bool changed = false;
        try {
            changed |= DeadBlockElimination().apply(prog);
            fmt::println("--dead block elimination--\n{}\n", prog);
            changed |= TrivialBlockReplacement().apply(prog);
            fmt::println("--trivial block replacement--\n{}\n", prog);
        } catch (const CompilerError& e) {
            fmt::println("TrivialBlockElimination failed: {}", e.what());
            fmt::println("Current program:\n{}", prog);
            throw;
        }
        return changed;
    }
};

}  // namespace ir::optim