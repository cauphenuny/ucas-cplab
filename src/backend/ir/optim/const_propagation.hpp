/// @brief Const Propagation Pass, must run after SSA construction

/// @note converts constexpr expressions to MOV inst,
/// then invoke Copy Propagation to further propagate constants.

#pragma once
#include "backend/ir/ir.h"
#include "backend/ir/optim/copy_propagation.hpp"
#include "backend/ir/type.hpp"
#include "copy_propagation.hpp"
#include "framework.hpp"
#include "utils/diagnosis.hpp"

#include <optional>
#include <variant>

namespace ir::optim {

struct ConstexprFolder {
    static std::optional<ConstexprValue> fold(UnaryInstOp op, const ConstexprValue& operand) {
        if (!operand.type.is<type::Primitive>()) return std::nullopt;
        return match(
            operand.val,
            [&](int v) -> std::optional<ConstexprValue> {
                if (op == UnaryInstOp::NEG) return ConstexprValue(-v);
                return std::nullopt;
            },
            [&](float v) -> std::optional<ConstexprValue> {
                if (op == UnaryInstOp::NEG) return ConstexprValue(-v);
                return std::nullopt;
            },
            [&](double v) -> std::optional<ConstexprValue> {
                if (op == UnaryInstOp::NEG) return ConstexprValue(-v);
                return std::nullopt;
            },
            [&](bool v) -> std::optional<ConstexprValue> {
                if (op == UnaryInstOp::NOT) return ConstexprValue(!v);
                return std::nullopt;
            },
            [&](const auto&) -> std::optional<ConstexprValue> { return std::nullopt; });
    }

    static std::optional<ConstexprValue> fold(InstOp op, const ConstexprValue& lhs,
                                              const ConstexprValue& rhs) {
        if (!lhs.type.is<type::Primitive>() || !rhs.type.is<type::Primitive>()) return std::nullopt;
        if (!(lhs.type == rhs.type)) return std::nullopt;

        return match(
            lhs.val,
            [&](int l) -> std::optional<ConstexprValue> {
                int r = std::get<int>(rhs.val);
                switch (op) {
                    case InstOp::ADD: return ConstexprValue(l + r);
                    case InstOp::SUB: return ConstexprValue(l - r);
                    case InstOp::MUL: return ConstexprValue(l * r);
                    case InstOp::DIV:
                        return r ? std::optional(ConstexprValue(l / r)) : std::nullopt;
                    case InstOp::MOD:
                        return r ? std::optional(ConstexprValue(l % r)) : std::nullopt;
                    case InstOp::LT: return ConstexprValue(l < r);
                    case InstOp::GT: return ConstexprValue(l > r);
                    case InstOp::LEQ: return ConstexprValue(l <= r);
                    case InstOp::GEQ: return ConstexprValue(l >= r);
                    case InstOp::EQ: return ConstexprValue(l == r);
                    case InstOp::NEQ: return ConstexprValue(l != r);
                    default: return std::nullopt;
                }
            },
            [&](float l) -> std::optional<ConstexprValue> {
                float r = std::get<float>(rhs.val);
                switch (op) {
                    case InstOp::ADD: return ConstexprValue(l + r);
                    case InstOp::SUB: return ConstexprValue(l - r);
                    case InstOp::MUL: return ConstexprValue(l * r);
                    case InstOp::DIV: return ConstexprValue(l / r);
                    case InstOp::LT: return ConstexprValue(l < r);
                    case InstOp::GT: return ConstexprValue(l > r);
                    case InstOp::LEQ: return ConstexprValue(l <= r);
                    case InstOp::GEQ: return ConstexprValue(l >= r);
                    case InstOp::EQ: return ConstexprValue(l == r);
                    case InstOp::NEQ: return ConstexprValue(l != r);
                    default: return std::nullopt;
                }
            },
            [&](double l) -> std::optional<ConstexprValue> {
                double r = std::get<double>(rhs.val);
                switch (op) {
                    case InstOp::ADD: return ConstexprValue(l + r);
                    case InstOp::SUB: return ConstexprValue(l - r);
                    case InstOp::MUL: return ConstexprValue(l * r);
                    case InstOp::DIV: return ConstexprValue(l / r);
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
            [&](const auto&) -> std::optional<ConstexprValue> { return std::nullopt; });
    }
};

struct ConstPropagation : Pass {
    bool apply(Program& prog) override {
        if (!prog.is_ssa) {
            throw COMPILER_ERROR("ConstPropagation requires SSA form");
        }
        bool result = false;
        bool changed;
        do {
            changed = false;
            for (auto& func : prog.getFuncs()) {
                changed |= propagate(*func);
            }
            changed |= CopyPropagation().apply(prog);
            result |= changed;
        } while (changed);
        return result;
    }

private:
    bool propagate(Func& func) {
        bool changed = false;
        for (auto& block : func.blocks()) {
            for (auto& inst : block->insts()) {
                if (auto unary = std::get_if<UnaryInst>(&inst)) {
                    if (auto c = std::get_if<ConstexprValue>(&unary->operand)) {
                        if (auto folded = ConstexprFolder::fold(unary->op, *c)) {
                            inst = UnaryInst{UnaryInstOp::MOV, unary->result, *folded};
                            changed = true;
                        }
                    }
                } else if (auto binary = std::get_if<BinaryInst>(&inst)) {
                    auto cl = std::get_if<ConstexprValue>(&binary->lhs);
                    auto cr = std::get_if<ConstexprValue>(&binary->rhs);
                    if (cl && cr) {
                        if (auto folded = ConstexprFolder::fold(binary->op, *cl, *cr)) {
                            inst = UnaryInst{UnaryInstOp::MOV, binary->result, *folded};
                            changed = true;
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
                        inst = UnaryInst{UnaryInstOp::MOV, phi->result, *common};
                        changed = true;
                    }
                }
            }

            if (auto branch = std::get_if<BranchExit>(&block->exit())) {
                if (auto c = std::get_if<ConstexprValue>(&branch->cond)) {
                    if (c->type.is<type::Primitive>() &&
                        std::holds_alternative<type::Bool>(c->type.as<type::Primitive>())) {
                        bool cond = std::get<bool>(c->val);
                        block->exit() = JumpExit{cond ? branch->true_target : branch->false_target};
                        changed = true;
                    }
                }
            }
        }
        return changed;
    }
};
}  // namespace ir::optim
