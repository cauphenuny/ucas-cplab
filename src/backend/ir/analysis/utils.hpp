#pragma once
#include "backend/ir/ir.h"
#include "utils/match.hpp"

namespace ir::analysis::utils {

inline auto is_named(const LeftValue& v) {
    return std::holds_alternative<NamedValue>(v);
}

/// @note: need to modify, so use reference wrapper, not just return const Alloc*
inline auto alloc_of(LeftValue& v) -> std::optional<std::reference_wrapper<const Alloc*>> {
    using T = std::optional<std::reference_wrapper<const Alloc*>>;
    return match(
        v,
        [&](NamedValue& n) -> T {
            return match(
                n.def, [&](const Alloc* a) -> T { return std::ref(a); },
                [](auto) -> T { return std::nullopt; });
        },
        [&](SSAValue& s) -> T { return std::ref(s.def); },
        [](TempValue&) -> T { return std::nullopt; });
};

// var: variable, a.k.a. LeftValue
inline auto as_var(Value& v) -> LeftValue* {
    return match(
        v, [](LeftValue& lv) -> LeftValue* { return &lv; },
        [](ConstexprValue& cv) -> LeftValue* { return nullptr; });
}

inline auto defined_var(Inst& inst) -> LeftValue* {
    return match(
        inst, [&](PhiInst& p) { return &p.result; },
        [&](BinaryInst& b) { return (b.op != InstOp::STORE) ? &b.result : nullptr; },
        [&](UnaryInst& u) { return (u.op != UnaryInstOp::STORE) ? &u.result : nullptr; },
        [&](CallInst& c) { return &c.result; });
}

inline auto used_vars(Inst& inst) -> std::vector<LeftValue*> {
    std::vector<LeftValue*> uses;
    match(
        inst,
        [&](PhiInst& p) {
            for (auto& [_, arg] : p.args) {
                if (auto lval = utils::as_var(arg); lval) uses.push_back(lval);
            }
        },
        [&](BinaryInst& b) {
            if (auto lval = utils::as_var(b.lhs); lval) uses.push_back(lval);
            if (auto lval = utils::as_var(b.rhs); lval) uses.push_back(lval);
            if (b.op == InstOp::STORE) uses.emplace_back(&b.result);
        },
        [&](UnaryInst& u) {
            if (auto lval = utils::as_var(u.operand); lval) uses.push_back(lval);
            if (u.op == UnaryInstOp::STORE) uses.emplace_back(&u.result);
        },
        [&](CallInst& c) {
            for (auto& arg : c.args) {
                if (auto lval = utils::as_var(arg); lval) uses.push_back(lval);
            }
        });
    return uses;
}

inline auto uses(Inst& inst) {
    std::vector<std::variant<Value*, LeftValue*>> uses;
    match(
        inst,
        [&](PhiInst& p) {
            for (auto& [_, arg] : p.args) {
                uses.emplace_back(&arg);
            }
        },
        [&](BinaryInst& b) {
            uses.emplace_back(&b.lhs);
            uses.emplace_back(&b.rhs);
            if (b.op == InstOp::STORE) uses.emplace_back(&b.result);
        },
        [&](UnaryInst& u) {
            uses.emplace_back(&u.operand);
            if (u.op == UnaryInstOp::STORE) uses.emplace_back(&u.result);
        },
        [&](CallInst& c) {
            for (auto& arg : c.args) {
                uses.emplace_back(&arg);
            }
        });
    return uses;
}

inline auto used_var(Exit& exit) -> LeftValue* {
    return match(
        exit, [](JumpExit&) -> LeftValue* { return nullptr; },
        [](BranchExit& c) -> LeftValue* { return utils::as_var(c.cond); },
        [](ReturnExit& r) -> LeftValue* { return utils::as_var(r.exp); });
}

inline auto used(Exit& exit) -> Value* {
    return match(
        exit, [](JumpExit&) -> Value* { return nullptr; },
        [](BranchExit& c) -> Value* { return &c.cond; },
        [](ReturnExit& r) -> Value* { return &r.exp; });
}

inline auto targets(Exit& exit) {
    using T = std::vector<std::reference_wrapper<Block*>>;
    return match(
        exit, [](JumpExit& j) -> T { return {std::ref(j.target)}; },
        [](BranchExit& b) -> T {
            T targets;
            if (b.true_target) targets.emplace_back(std::ref(b.true_target));
            if (b.false_target) targets.emplace_back(std::ref(b.false_target));
            return targets;
        },
        [](ReturnExit&) -> T { return {}; });
}

inline auto vars(Inst& inst) {
    auto ret = used_vars(inst);
    if (auto def = defined_var(inst); def) ret.push_back(def);
    return ret;
}

inline bool has_side_effect(Inst& inst) {
    return match(
        inst, [&](const CallInst&) { return true; },  // function call may have side effects
        [&](const UnaryInst& u) { return false; }, [&](const BinaryInst& b) { return false; },
        [&](const auto&) { return false; });
}

}  // namespace ir::analysis::utils