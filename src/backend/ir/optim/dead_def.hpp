/// @brief Dead Definition Elimination Pass, requires SSA

#pragma once
#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "framework.hpp"

#include <list>
#include <queue>
#include <unordered_map>
#include <utility>

namespace ir::optim {

struct DeadDefElimination : Pass {
    bool apply(Program& prog) override {
        if (!prog.is_ssa) {
            throw COMPILER_ERROR("DeadDefElimination requires SSA form");
        }
        bool changed = false;
        while (eliminate(prog)) changed = true;
        return changed;
    }

private:
    bool eliminate(Program& prog) {
        using DefSite = std::pair<Block*, std::list<Inst>::iterator>;
        std::unordered_map<LeftValue, DefSite> def_site;

        std::unordered_map<LeftValue, size_t> used_count;

        for (auto& func : prog.funcs()) {
            for (auto& block : func->blocks()) {
                for (auto it = block->insts().begin(); it != block->insts().end(); ++it) {
                    auto& inst = *it;
                    for (auto var : utils::used_vars(inst)) {
                        used_count[*var]++;
                    }
                    if (auto def = utils::defined_var(inst); def) {
                        if (def_site.count(*def)) {
                            throw COMPILER_ERROR(
                                fmt::format("LeftValue {} defined multiple times", *def));
                        }
                        def_site[*def] = {block.get(), it};
                    }
                }
                if (auto var = utils::used_var(block->exit()); var) {
                    used_count[*var]++;
                }
            }
        }

        std::queue<DefSite> dead_queue;
        for (auto& func : prog.funcs()) {
            for (auto& block : func->blocks()) {
                for (auto it = block->insts().begin(); it != block->insts().end(); ++it) {
                    auto& inst = *it;
                    if (auto def = utils::defined_var(inst); def) {
                        if (used_count[*def] == 0 && !utils::has_side_effect(inst)) {
                            dead_queue.emplace(block.get(), it);
                        }
                    }
                }
            }
        }

        if (dead_queue.empty()) {
            return false;
        }

        while (!dead_queue.empty()) {
            auto it = dead_queue.front();
            dead_queue.pop();

            auto& [block, inst_it] = it;
            auto& inst = *inst_it;

            for (auto use : utils::used_vars(inst)) {
                used_count[*use]--;
                if (used_count[*use] == 0) {
                    dead_queue.push(def_site[*use]);
                }
            }

            block->erase(inst_it);
        }
        return true;
    }
};

}  // namespace ir::optim