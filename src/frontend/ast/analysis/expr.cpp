#include "backend/ir/type.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/op.hpp"
#include "semantic_ast.h"
#include "utils/error.hpp"
#include "utils/match.hpp"
#include "utils/traits.hpp"
#include "utils/tui.h"

#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ast {

void SemanticAST::analysis(const LVal* lid, const Type& upperbound, bool readonly) {
    if (!upperbound.is<ir::type::Func>()) {
        auto symdef = lookup(lid->name);
        if (!symdef) {
            throw SemanticError(lid->loc, fmt::format("undefined variable '{}'", lid->name));
        }
        if (this->readonly_defs.count(*symdef) && !readonly) {
            throw SemanticError(lid->loc,
                                fmt::format("can not use constant '{}' as left val", lid->name));
        }
        match(*symdef, [&](const auto& def) {
            defs[lid] = def;
            // decay based on source constness only, not context readonly
            // (readonly means "not an lvalue", not "pointer should be readonly")
            bool is_const = readonly_defs.count(def);
            types[lid] = types[def].decay(is_const);
        });
    } else {
        if (!funcs.count(lid->name)) {
            throw SemanticError(lid->loc, fmt::format("undefined function '{}'", lid->name));
        }
        auto [funcdef, is_builtin] = funcs[lid->name];
        defs[lid] = funcdef;
        types[lid] = types[funcdef];
    }
    checkType(lid, upperbound);
}

void SemanticAST::analysis(const LValExp* lval, const Type& upperbound, bool readonly) {
    match(lval->val, [&](const auto& subexp) {
        analysis(&subexp, upperbound, readonly);
        types[lval] = types[&subexp];
    });
}

void SemanticAST::analysis(const Exp* exp, const Type& upperbound, bool readonly) {
    match(*exp, [&](const auto& subexp) {
        analysis(&subexp, upperbound, readonly);
        types[exp] = types[&subexp];
    });
}

void SemanticAST::analysis(const CallExp* call, const Type& upperbound, bool readonly) {
    auto func = &call->func;
    analysis(func, ir::type::Func(NEVER, ANY).toBoxed(), true);
    auto args = &call->args;
    auto param_type = types[func].as<ir::type::Func>().params.as<ir::type::Product>();
    if (args->size() != param_type.items().size()) {
        throw SemanticError(call->loc,
                            fmt::format("function `{}` expects {} arguments, but {} provided",
                                        func->name, param_type.items().size(), args->size()));
    }
    analysis(args, param_type);
    types[call] = types[func].as<ir::type::Func>().ret;
    checkType(call, upperbound);
}

void SemanticAST::analysis(const ConstExp* const_exp, const Type& upperbound, bool readonly) {
    types[const_exp] =
        match(*const_exp, [&](auto val) { return ir::type::construct<decltype(val)>(); });
}

void SemanticAST::analysis(const PrimaryExp* primary, const Type& upperbound, bool readonly) {
    match(primary->exp, [&](const auto& subexp) {
        analysis(&subexp, upperbound, readonly);
        types[primary] = types[&subexp];
    });
    checkType(primary, upperbound);
}

void SemanticAST::analysis(const UnaryExp* unary_exp, const Type& upperbound, bool readonly) {
    auto exp = &unary_exp->exp;
    Type operand;
    switch (unary_exp->op) {
        case UnaryOp::PLUS:
        case UnaryOp::MINUS: operand = NUM; break;
        case UnaryOp::NOT: operand = BOOL;
    }
    analysis(exp, operand, true);
    types[unary_exp] = types[exp];
    checkType(unary_exp, upperbound);
}

void SemanticAST::analysis(const BinaryExp* binary_exp, const Type& upperbound, bool readonly) {
    using namespace ast;
    auto left = &binary_exp->left;
    auto right = &binary_exp->right;
    Type lhs_bound, rhs_bound;
    switch (binary_exp->op) {
        case BinaryOp::AND:
        case BinaryOp::OR: rhs_bound = BOOL; break;
        case BinaryOp::INDEX: rhs_bound = INT; break;
        default: rhs_bound = NUM; break;
    }
    analysis(right, rhs_bound, true);
    switch (binary_exp->op) {
        case BinaryOp::INDEX:
            lhs_bound = ir::type::Reference::slice(upperbound, readonly).toBoxed();
            break;
        case BinaryOp::AND:
        case BinaryOp::OR: lhs_bound = BOOL; break;
        default: lhs_bound = NUM; break;
    }
    analysis(left, lhs_bound, readonly || binary_exp->op != BinaryOp::INDEX);
    if (binary_exp->op != BinaryOp::INDEX) {
        if (!(types[left] <= types[right]) && !(types[right] <= types[left])) {
            throw SemanticError(
                binary_exp->loc,
                fmt::format("type error: cannot apply operator `{}` to `{}` and `{}`" DIM
                            " (at {})" NONE,
                            binary_exp->op, types[left], types[right], *binary_exp));
        }
    }
    switch (binary_exp->op) {
        case BinaryOp::EQ:
        case BinaryOp::NEQ:
        case BinaryOp::LT:
        case BinaryOp::GT:
        case BinaryOp::LEQ:
        case BinaryOp::GEQ: types[binary_exp] = BOOL; break;
        case BinaryOp::INDEX: {
            const auto& ptr = types[left].as<ir::type::Reference>();  // already decayed
            types[binary_exp] = ptr.elem;
            types[binary_exp] = types[binary_exp].decay(ptr.readonly);
            break;
        }
        default:
            types[binary_exp] = types[left] <= types[right] ? types[right] : types[left];
            break;
    }
    checkType(binary_exp, upperbound);
}

void SemanticAST::analysis(const ExpBox* exp_box, const Type& upperbound, bool readonly) {
    auto exp = exp_box->exp.get();
    analysis(exp, upperbound, readonly);
    types[exp_box] = types[exp];
    checkType(exp_box, upperbound);
}

}  // namespace ast
