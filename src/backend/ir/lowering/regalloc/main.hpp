#pragma once

#include "backend/ir/ir.h"
#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/lowering/regalloc/colorize.hpp"
#include "backend/ir/lowering/regalloc/graph.hpp"
#include "backend/ir/lowering/regalloc/precolorize.hpp"
#include "backend/ir/lowering/regalloc/spill.hpp"
#include "backend/ir/op.hpp"
#include "backend/ir/transform/framework.hpp"

#include <unordered_map>
#include <utility>
#include <vector>

namespace ir::lowering {

struct RegisterAllocation : transform::NonSSAPass {
    using TargetABI = lowering::TargetABI;
    RegisterAllocation(TargetABI abi, bool verbose = false)
        : abi(std::move(abi)), verbose(verbose) {}
    TargetABI abi;
    bool verbose;

    bool apply(Program& prog, transform::NonSSAPassContext& ctx) override {
        Precolorize precolor(abi, verbose);
        precolor.apply(prog, ctx);
        precolored = precolor.precolored;

        for (auto& [key, alloc] : precolored) colored[alloc->value()] = key.second;

        while (true) {
            auto [general, floating] = InterfereGraph::build(prog, precolor.precolored, abi);
            if (verbose) {
                fmt::print(stderr, "General Graph: {}\n", general);
                fmt::print(stderr, "Floating-point Graph: {}\n", floating);
            }
            auto [general_spills, general_colors] = BriggsAllocator(general).colorize();
            auto [floating_spills, floating_colors] = BriggsAllocator(floating).colorize();
            if (general_spills.empty() && floating_spills.empty()) {
                colored = merge(std::move(general_colors), std::move(floating_colors));
                break;
            }
            Spill(general_spills, verbose).apply(prog, ctx);
            Spill(floating_spills, verbose).apply(prog, ctx);

            if (verbose) {
                for (auto& [value, id] : colored) {
                    auto& graph = is_fp(type_of(value)) ? floating : general;
                    graph.pin(value, id);
                }
                fmt::print(stderr, "Colorized General Graph: {}\n", general);
                fmt::print(stderr, "Colorized Floating-point Graph: {}\n", floating);
            }
        }

        return true;
    }

    static ColorMap merge(ColorMap&& general, ColorMap&& floating) {
        ColorMap merged;
        merged.insert(general.begin(), general.end());
        merged.insert(floating.begin(), floating.end());
        return merged;
    }

    ColorMap colored;
    PrecolorVars precolored;
};

/// @brief replace colored values by their assigned registers
template <typename T> struct RegisterReplacement : transform::Pass<T> {
    RegisterReplacement(ColorMap color, PrecolorVars precolored)
        : color(std::move(color)), precolored(std::move(precolored)) {}
    const ColorMap color;
    const PrecolorVars precolored;
    bool apply(Program& program, T& ctx) override {
        using namespace ir::analysis;
        bool changed = false;
        auto replace = [&](const LeftValue& v, bool retain_valtype = true) -> LeftValue {
            if (!color.count(v)) return v;
            auto alloc = precolored.at({type_of(v), color.at(v)});
            auto value = alloc->value();
            if (retain_valtype) value.type = type_of(v);
            changed = true;
            return value;
        };
        for (auto& func : program.funcs()) {
            for (auto& block : func->blocks()) {
                for (auto& inst : block->insts()) {
                    auto new_inst = inst;
                    if (auto var = utils::defined_var(new_inst)) *var = replace(*var, false);
                    for (auto var : utils::used_vars(new_inst)) *var = replace(*var);
                    block->replace(&inst, new_inst);
                }
                auto exit = block->exit();
                if (auto use = utils::used_var(exit)) {
                    *use = replace(*use);
                }
                block->setExit(exit);
            }
        }
        return changed;
    }
};

/// @brief remove redundant moves.
template <typename T> struct RedundantMoveElimination : transform::Pass<T> {
    bool apply(Program& program, T& ctx) override {
        using namespace ir::analysis;
        bool changed = false;
        for (auto& func : program.funcs()) {
            for (auto& block : func->blocks()) {
                auto& insts = block->insts();
                for (auto it = insts.begin(); it != insts.end();) {
                    it = [&] {
                        if (auto mov = std::get_if<UnaryInst>(&*it)) {
                            if (mov->op != UnaryInstOp::MOV && mov->op != UnaryInstOp::CONVERT) {
                                return std::next(it);
                            }
                            if (auto lval = std::get_if<LeftValue>(&mov->operand)) {
                                if (*lval == mov->result) {
                                    changed = true;
                                    return block->erase(it);
                                }
                            }
                        }
                        return std::next(it);
                    }();
                }
            }
        }
        return changed;
    }
};

}  // namespace ir::lowering