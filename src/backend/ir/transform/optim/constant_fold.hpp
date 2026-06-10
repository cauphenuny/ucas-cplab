/// @brief Const Propagation Pass, requires SSA

/// @note converts constexpr expressions to MOV inst,
/// then invoke Copy Propagation to further propagate constants.

#pragma once
#include "../framework.hpp"
#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "backend/ir/type.hpp"

#include <memory>
#include <optional>
#include <variant>

namespace ir::transform {

struct ConstexprFolder {
    static std::optional<ConstexprValue> fold(UnaryInstOp op, const ConstexprValue& operand) {
        if (!operand.type.is<type::Primitive>()) return std::nullopt;
        return match(
            operand.val,
            [&](auto v) -> std::optional<ConstexprValue> {
                if (op == UnaryInstOp::NEG) return ConstexprValue(-v);
                return std::nullopt;
            },
            [&](bool v) -> std::optional<ConstexprValue> {
                if (op == UnaryInstOp::NOT) return ConstexprValue(!v);
                return std::nullopt;
            },
            [&](const std::monostate&) -> std::optional<ConstexprValue> { return std::nullopt; },
            [&](const std::unique_ptr<std::byte[]>&) -> std::optional<ConstexprValue> {
                return std::nullopt;
            });
    }

    static std::optional<ConstexprValue> fold(InstOp op, const ConstexprValue& lhs,
                                              const ConstexprValue& rhs) {
        if (!lhs.type.is<type::Primitive>() || !rhs.type.is<type::Primitive>()) return std::nullopt;
        if (!(lhs.type == rhs.type)) return std::nullopt;

        return match(
            lhs.val,
            [&](auto l) -> std::optional<ConstexprValue> {
                using T = decltype(l);
                auto r = std::get<T>(rhs.val);
                switch (op) {
                    case InstOp::ADD: return ConstexprValue(l + r);
                    case InstOp::SUB: return ConstexprValue(l - r);
                    case InstOp::MUL: return ConstexprValue(l * r);
                    case InstOp::DIV:
                        return r ? std::optional(ConstexprValue(l / r)) : std::nullopt;
                    case InstOp::MOD:
                        if constexpr (std::is_integral_v<T>) {
                            return r ? std::optional(ConstexprValue(l % r)) : std::nullopt;
                        } else {
                            return std::nullopt;
                        }
                    case InstOp::LT: return ConstexprValue(l < r);
                    case InstOp::GT: return ConstexprValue(l > r);
                    case InstOp::LEQ: return ConstexprValue(l <= r);
                    case InstOp::GEQ: return ConstexprValue(l >= r);
                    case InstOp::EQ: return ConstexprValue(l == r);
                    case InstOp::NEQ: return ConstexprValue(l != r);
                    default: return std::nullopt;
                }
            },
            [&](bool l) -> std::optional<ConstexprValue> {
                bool r = std::get<bool>(rhs.val);
                switch (op) {
                    case InstOp::AND: return ConstexprValue(l && r);
                    case InstOp::OR: return ConstexprValue(l || r);
                    case InstOp::EQ: return ConstexprValue(l == r);
                    case InstOp::NEQ: return ConstexprValue(l != r);
                    default: return std::nullopt;
                }
            },
            [&](const std::monostate&) -> std::optional<ConstexprValue> { return std::nullopt; },
            [&](const std::unique_ptr<std::byte[]>&) -> std::optional<ConstexprValue> {
                return std::nullopt;
            });
    }
};

template <typename T> struct ConstantFolding : Pass<T> {
    bool apply(Program& prog, T& ctx) override {
        bool changed = false;
        for (auto& global : prog.globals()) {
            if (global->comptime && !global->reference) {
                changed |= ctx.ud.replace_all_uses_with(global->value(), *global->init);
            }
        }
        for (auto& func : prog.funcs()) {
            changed |= propagate(*func, ctx);
        }
        return changed;
    }

private:
    bool propagate(Func& func, T& ctx) {
        bool changed = false;
        for (auto& local : func.locals()) {
            if (local->comptime && !local->reference) {
                changed |= ctx.ud.replace_all_uses_with(local->value(), *local->init);
            }
        }
        for (auto& block : func.blocks()) {
            for (auto& inst : block->insts()) {
                if (!utils::defined_var(inst)) continue;
                bool inst_changed = false;
                auto new_inst = inst;
                if (auto unary = std::get_if<UnaryInst>(&inst)) {
                    if (auto c = std::get_if<ConstexprValue>(&unary->operand)) {
                        if (auto folded = ConstexprFolder::fold(unary->op, *c)) {
                            inst_changed = true;
                            new_inst = UnaryInst{UnaryInstOp::MOV, unary->result, *folded};
                        }
                    }
                } else if (auto binary = std::get_if<BinaryInst>(&inst)) {
                    auto cl = std::get_if<ConstexprValue>(&binary->lhs);
                    auto cr = std::get_if<ConstexprValue>(&binary->rhs);
                    if (cl && cr) {
                        if (auto folded = ConstexprFolder::fold(binary->op, *cl, *cr)) {
                            inst_changed = true;
                            new_inst = UnaryInst{UnaryInstOp::MOV, binary->result, *folded};
                        }
                    } else if (binary->result) {
                        // Identity element simplification: x+0, x-0, x*1, 0+x, 1*x
                        auto is_zero = [](const ConstexprValue& c) {
                            return std::visit(
                                [](const auto& v) -> bool {
                                    if constexpr (std::is_same_v<std::decay_t<decltype(v)>,
                                                                 std::monostate> ||
                                                  std::is_same_v<std::decay_t<decltype(v)>,
                                                                 std::unique_ptr<std::byte[]>>)
                                        return false;
                                    else
                                        return v == decltype(v){0};
                                },
                                c.val);
                        };
                        auto is_one = [](const ConstexprValue& c) {
                            return std::visit(
                                [](const auto& v) -> bool {
                                    if constexpr (std::is_same_v<std::decay_t<decltype(v)>,
                                                                 std::monostate> ||
                                                  std::is_same_v<std::decay_t<decltype(v)>,
                                                                 std::unique_ptr<std::byte[]>> ||
                                                  std::is_same_v<std::decay_t<decltype(v)>, bool>)
                                        return false;
                                    else
                                        return v == decltype(v){1};
                                },
                                c.val);
                        };
                        std::optional<Value> identity_result;
                        switch (binary->op) {
                            case InstOp::ADD:
                                if (cr && is_zero(*cr))
                                    identity_result = binary->lhs;
                                else if (cl && is_zero(*cl))
                                    identity_result = binary->rhs;
                                break;
                            case InstOp::SUB:
                                if (cr && is_zero(*cr)) identity_result = binary->lhs;
                                break;
                            case InstOp::MUL:
                                if (cr && is_one(*cr))
                                    identity_result = binary->lhs;
                                else if (cl && is_one(*cl))
                                    identity_result = binary->rhs;
                                break;
                            default: break;
                        }
                        if (identity_result) {
                            inst_changed = true;
                            new_inst =
                                UnaryInst{UnaryInstOp::MOV, binary->result, *identity_result};
                        }
                    }
                } else if (auto phi = std::get_if<PhiInst>(&inst)) {
                    std::optional<ConstexprValue> common;
                    bool foldable = !phi->args.empty();
                    for (auto& [_, val] : phi->args) {
                        if (auto c = std::get_if<ConstexprValue>(&val)) {
                            if (!common) {
                                common = *c;
                            } else if (!(*common == *c)) {
                                foldable = false;
                                break;
                            }
                        } else {
                            foldable = false;
                            break;
                        }
                    }
                    if (foldable && common) {
                        inst_changed = true;
                        new_inst = UnaryInst{UnaryInstOp::MOV, phi->result, *common};
                    }
                }
                if (inst_changed) {
                    changed = true;
                    block->replace(&inst, new_inst);
                }
            }

            if (auto branch = std::get_if<BranchExit>(&block->exit())) {
                if (auto c = std::get_if<ConstexprValue>(&branch->cond)) {
                    if (c->type.is<type::Primitive>() &&
                        std::holds_alternative<type::Int1>(c->type.as<type::Primitive>())) {
                        bool cond = std::get<bool>(c->val);
                        block->setExit(JumpExit{cond ? branch->true_target : branch->false_target});
                        changed = true;
                    }
                }
            }
        }
        return changed;
    }
};
}  // namespace ir::transform
