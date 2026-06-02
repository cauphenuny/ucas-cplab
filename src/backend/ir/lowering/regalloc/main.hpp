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

/// @brief replace colored values by their assigned registers, also remove redundant moves.
template <typename T> struct RedundantMoveElimination : transform::Pass<T> {
    RedundantMoveElimination(ColorMap color, PrecolorVars precolored)
        : color(std::move(color)), precolored(std::move(precolored)) {}
    const ColorMap color;
    const PrecolorVars precolored;
    bool apply(Program& program, T& ctx) override {
        bool changed = false;
        for (auto& func : program.funcs()) {
            for (auto& block : func->blocks()) {
                auto& insts = block->insts();
                for (auto it = insts.begin(); it != insts.end();) {
                    if (auto mov = std::get_if<UnaryInst>(&*it)) {
                        if (mov->op != UnaryInstOp::MOV) {
                            ++it;
                            continue;
                        }
                        if (auto src = std::get_if<LeftValue>(&mov->operand)) {
                            if (!color.count(*src) || !color.count(*mov->result)) {
                                ++it;
                                continue;
                            }
                            auto src_color = color.at(*src);
                            auto dst_color = color.at(*mov->result);
                            if (src_color == dst_color) {
                                it = block->erase(it);
                                changed = true;
                                continue;
                            }
                        }
                    }
                    ++it;
                }
            }
        }
        for (auto& [value, id] : color) {
            auto type = type_of(value);
            LeftValue target = precolored.at({type, id})->value();
            match(target, [&](auto& val) { val.type = type; });
            if (!(target == value)) {
                ctx.ud.replace_all_defs_with(value, target);
                ctx.ud.replace_all_uses_with(value, target);
            }
        }
        return changed;
    }
};

}  // namespace ir::lowering