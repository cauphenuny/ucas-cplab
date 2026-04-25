/// @brief Common Subexpressions Elimination, requires SSA

/// @note requires no dead block (dataflow analysis needs it)

#pragma once

#include "backend/ir/analysis/dominance.hpp"
#include "backend/ir/ir.h"
#include "backend/ir/op.hpp"
#include "framework.hpp"

#include <cstdint>
#include <tuple>
#include <unordered_map>

namespace ir::optim {

struct CommonSubexprElimination : Pass {
    bool apply(Program& prog) override {
        if (!prog.is_ssa) {
            throw COMPILER_ERROR("CommonSubexprElimination requires SSA form");
        }
        bool changed = false;
        for (auto& func : prog.funcs()) {
            changed |= eliminate(*func, prog);
        }
        return changed;
    }
    using Expr = std::tuple<uint16_t, uint32_t, uint32_t>;  // op, arg1, arg2
    struct ExprHash {
        auto operator()(const Expr& expr) const -> size_t {
            auto [op, arg1, arg2] = expr;
            size_t h1 = std::hash<uint16_t>{}(op);
            size_t h2 = std::hash<uint32_t>{}(arg1);
            size_t h3 = std::hash<uint32_t>{}(arg2);
            return h1 ^ (h2 << 1) ^ (h3 << 2);
        }
    };

    struct Context {
        std::unordered_map<Value, uint32_t> value_num;
        std::unordered_map<Expr, uint32_t, ExprHash> expr_num;
        std::unordered_map<uint32_t, Value> num_value;
        Context() {
            value_num[ConstexprValue()] = 0;  // void = 0
            num_value[0] = ConstexprValue();
        }
    };

private:
    bool eliminate(Func& func, Program& prog) {
        auto cfg = analysis::ControlFlowGraph(func);
        auto dom_tree =
            analysis::DominanceTree(analysis::DataFlow<analysis::flow::Dominance>(cfg, prog));
        return eliminate(*func.entrance(), Context{}, dom_tree);
    }

    bool eliminate(Block& block, Context ctx, DominanceTree& dom_tree) {
        bool changed = false;
        auto insert = [&](const Value& val) {
            uint32_t id = ctx.value_num.size();
            ctx.value_num[val] = id;
            ctx.num_value[id] = val;
        };
        auto convert = [&](uint16_t op, uint32_t arg1, uint32_t arg2, auto inst) -> Inst {
            Expr expr{op, arg1, arg2};
            if (ctx.expr_num.count(expr)) {
                changed = true;
                ctx.value_num[inst.result] = ctx.expr_num[expr];
                return UnaryInst{UnaryInstOp::MOV, inst.result, ctx.num_value[ctx.expr_num[expr]]};
            } else {
                insert(inst.result);
                ctx.expr_num[expr] = ctx.value_num[inst.result];
                return inst;
            }
        };
        auto lookup = [&](const Value& val) -> uint32_t {
            if (!ctx.value_num.count(val)) {
                insert(val);
            }
            return ctx.value_num[val];
        };
        auto encode = [&](const auto& op) -> uint16_t {
            using OpType = std::decay_t<decltype(op)>;
            if constexpr (std::is_same_v<OpType, UnaryInstOp>) {
                return static_cast<uint16_t>(op);
            } else if constexpr (std::is_same_v<OpType, InstOp>) {
                return static_cast<uint16_t>(op) |
                       0x1000;  // offset to avoid conflict with unary op
            }
        };
        for (auto& inst : block.insts()) {
            block.replace(
                &inst,
                Match{inst}([&](const auto&) { return inst; },
                            [&](const UnaryInst& unary) -> Inst {
                                if (unary.op == UnaryInstOp::LOAD || unary.op == UnaryInstOp::MOV) {
                                    return unary;  // skip memory access and direct assignment
                                }
                                uint32_t operand_num = lookup(unary.operand);
                                return convert(encode(unary.op), operand_num, 0, unary);
                            },
                            [&](const BinaryInst& binary) -> Inst {
                                if (binary.op == InstOp::LOAD_ELEM || binary.op == InstOp::STORE) {
                                    return binary;  // skip memory access
                                }
                                uint32_t lhs_num = lookup(binary.lhs);
                                uint32_t rhs_num = lookup(binary.rhs);
                                auto inst = convert(encode(binary.op), lhs_num, rhs_num, binary);
                                if (commutative(binary.op)) {
                                    ctx.expr_num[Expr{encode(binary.op), rhs_num, lhs_num}] =
                                        ctx.expr_num[Expr{encode(binary.op), lhs_num, rhs_num}];
                                }
                                return inst;
                            }));
        }
        for (auto& dom : dom_tree.children(&block)) {
            changed |= eliminate(*dom, ctx, dom_tree);
        }
        return changed;
    }
};

}  // namespace ir::optim