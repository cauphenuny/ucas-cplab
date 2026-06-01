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
    RegisterAllocation(TargetABI abi) : abi(std::move(abi)) {}
    TargetABI abi;

    bool apply(Program& prog, transform::NonSSAPassContext& ctx) override {
        Precolorize precolor(abi);
        precolor.apply(prog, ctx);
        precolored = precolor.precolored;
        fmt::println("After precoloring:\n{}", prog);

        while (true) {
            auto [general, floating] = InterfereGraph::build(prog, precolor.precolored, abi);
            fmt::print(stderr, "general graph: {}\n", general);
            fmt::print(stderr, "floating-point graph: {}\n", floating);
            auto [general_spills, general_colors] = BriggsAllocator(general).colorize();
            auto [floating_spills, floating_colors] = BriggsAllocator(floating).colorize();
            if (general_spills.empty() && floating_spills.empty()) {
                colored = merge(std::move(general_colors), std::move(floating_colors));
                break;
            }
            Spill(general_spills).apply(prog, ctx);
            Spill(floating_spills).apply(prog, ctx);
        }

        fmt::println("Colorized: {}", colored);

        for (auto& [key, alloc] : precolor.precolored) {
            auto& [_, id] = key;
            colored[alloc->value()] = id;
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

template <typename T> struct RedundantMoveElimination : transform::Pass<T> {
    RedundantMoveElimination(ColorMap color) : color(std::move(color)) {}
    const ColorMap color;
    bool apply(Program& program, T&) {
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
                            auto src_color = color.at(*src);
                            auto dst_color = color.at(*mov->result);
                            if (src_color == dst_color) {
                                it = insts.erase(it);
                                changed = true;
                                continue;
                            }
                        }
                    }
                    ++it;
                }
            }
        }
        return changed;
    }
};

}  // namespace ir::lowering