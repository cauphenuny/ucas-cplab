/// @brief Dead Allocation Elimination Pass

#pragma once

#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "framework.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ir::optim {
struct DeadAllocElimination : SSAPass {
    bool apply(Program& prog, SSAPassContext& ctx) override {
        if (!prog.is_ssa) {
            throw COMPILER_ERROR("DeadAllocElimination requires SSA form");
        }
        bool changed = false;
        std::unordered_map<const Alloc*, bool> referenced;
        for (auto& func : prog.funcs()) {
            for (auto var : analysis::utils::vars(*func)) {
                if (auto named = std::get_if<NamedValue>(var); named) {
                    if (auto alloc = std::get_if<const Alloc*>(&named->def); alloc) {
                        referenced[*alloc] = true;
                    }
                }
                if (auto ssa = std::get_if<SSAValue>(var); ssa) {
                    referenced[ssa->def] = true;
                }
            }
            std::vector<std::unique_ptr<Alloc>> kept_locals;
            for (auto& alloc : func->locals()) {
                if (referenced.count(alloc.get())) {
                    kept_locals.push_back(std::move(alloc));
                } else {
                    changed = true;
                }
            }
            func->locals() = std::move(kept_locals);
        }
        std::vector<std::unique_ptr<Alloc>> kept_globals;
        for (auto& global : prog.globals()) {
            if (referenced.count(global.get())) {
                kept_globals.push_back(std::move(global));
            } else {
                changed = true;
            }
        }
        prog.globals() = std::move(kept_globals);
        return changed;
    }
};

template <typename Context> struct DeadTempEliminationImpl : Pass<Context> {
    bool apply(Program& prog, Context& ctx) override {
        bool changed = false;
        for (auto& func : prog.funcs()) {
            std::unordered_map<size_t, size_t> remap;
            for (size_t i = 0; i < func->temps().size(); i++) {
                auto& temp_info = func->temps()[i];
                auto temp = TempValue{.type = temp_info.type, .id = i, .func = func.get()};
                if (has_elem(ctx.ud.def_of(temp)) || ctx.ud.uses_of(temp).size()) {
                    remap[i] = remap.size();
                }
            }
            std::vector<Func::TempInfo> new_temps(remap.size());
            if (new_temps.size() != func->temps().size()) changed = true;
            // Must process in ascending order of `from` to avoid aliasing:
            // renaming a→b then b→c must be done as (a→b) after (b→c), i.e. a first if a<b.
            std::vector<std::pair<size_t, size_t>> ordered(remap.begin(), remap.end());
            std::sort(ordered.begin(), ordered.end());
            for (auto& [from, to] : ordered) {
                auto from_val =
                    TempValue{.type = func->temps()[from].type, .id = from, .func = func.get()};
                auto to_val =
                    TempValue{.type = func->temps()[to].type, .id = to, .func = func.get()};
                new_temps[to] = func->temps()[from];
                if (from != to) {
                    changed = true;
                    ctx.ud.replace_all_uses_with(from_val, LeftValue{to_val});
                    ctx.ud.replace_all_defs_with(from_val, LeftValue{to_val});
                }
            }
            func->temps() = std::move(new_temps);
        }
        return changed;
    }

private:
    static bool has_elem(std::optional<DefSite> def) {
        return (bool)def;
    }
    static bool has_elem(const std::unordered_set<DefSite>& defs) {
        return !defs.empty();
    }
};

using DeadTempElimination = DeadTempEliminationImpl<SSAPassContext>;
using NonSSADeadTempElimination = DeadTempEliminationImpl<NonSSAPassContext>;

}  // namespace ir::optim