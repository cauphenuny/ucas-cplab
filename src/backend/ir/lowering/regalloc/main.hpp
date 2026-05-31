#pragma once

#include "backend/ir/ir.h"
#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/lowering/regalloc/colorize.hpp"
#include "backend/ir/lowering/regalloc/interfere.hpp"
#include "backend/ir/lowering/regalloc/precolorize.hpp"
#include "backend/ir/lowering/regalloc/scanmov.hpp"
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
        PreColorize precolor(abi);
        precolor.apply(prog, ctx);
        proxies = precolor.proxies;
        fmt::println("After precoloring:\n{}", prog);

        while (true) {
            auto graph = InterfereGraph::build(prog, precolor.proxies, abi);
            auto moves = scan_move(prog);
            fmt::print(stderr, "graph: {}\nmoves: {}\n", graph, moves);
            auto [spills, colors] = BriggsAllocator(graph, moves).colorize();
            if (spills.empty()) {
                this->colored = std::move(colors);
                break;
            }
            Spill(spills).apply(prog, ctx);
        }

        fmt::println("Colorized: {}", colored);

        for (auto& [key, alloc] : precolor.proxies) {
            auto& [type, id] = key;
            colored[alloc->value()] = id;
        }
        return true;
    }

    ColorMap colored;
    ProxyMap proxies;
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
                    if (auto unary = std::get_if<UnaryInst>(&*it)) {
                        if (unary->op != UnaryInstOp::MOV) {
                            ++it;
                            continue;
                        }
                        if (auto src = std::get_if<LeftValue>(&unary->operand)) {
                            auto src_color = color.at(*src);
                            auto dst_color = color.at(unary->result);
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