#include "semantic_ast.h"
#include "utils/error.hpp"

namespace ast {

void SemanticAST::analysis(const LVal* lid, const Type& upperbound, bool immutable) {
    if (!upperbound.is<adt::Func>()) {
        auto symdef = lookup(lid->name);
        if (!symdef) {
            throw SemanticError(lid->loc, fmt::format("undefined variable '{}'", lid->name));
        }
        match(*symdef, [&](const auto& def) {
            defs[lid] = def;
            types[lid] = types[def].decay(immutable);
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

void SemanticAST::analysis(const LValExp* lval, const Type& upperbound, bool immutable) {
    match(lval->val, [&](const auto& subexp) {
        analysis(&subexp, upperbound, immutable);
        types[lval] = types[&subexp];
    });
}

void SemanticAST::analysis(const Exp* exp, const Type& upperbound, bool immutable) {
    match(*exp, [&](const auto& subexp) {
        analysis(&subexp, upperbound, immutable);
        types[exp] = types[&subexp];
    });
}

void SemanticAST::analysis(const CallExp* call, const Type& upperbound, bool immutable) {
    auto func = &call->func;
    analysis(func, adt::Func(NEVER, ANY).toBoxed(), immutable);
    auto args = &call->args;
    auto param_type = types[func].as<adt::Func>().params.as<adt::Product>();
    if (args->size() != param_type.items().size()) {
        throw SemanticError(call->loc,
                            fmt::format("function `{}` expects {} arguments, but {} provided",
                                        func->name, param_type.items().size(), args->size()));
    }
    analysis(args, param_type);
    types[call] = types[func].as<adt::Func>().ret;
    checkType(call, upperbound);
}

void SemanticAST::analysis(const ConstExp* const_exp, const Type& upperbound, bool immutable) {
    types[const_exp] =
        match(*const_exp, [&](auto val) { return adt::construct<const decltype(val)>(); });
}

void SemanticAST::analysis(const PrimaryExp* primary, const Type& upperbound, bool immutable) {
    match(primary->exp, [&](const auto& subexp) {
        analysis(&subexp, upperbound, immutable);
        types[primary] = types[&subexp];
    });
    checkType(primary, upperbound);
}

void SemanticAST::analysis(const UnaryExp* unary_exp, const Type& upperbound, bool immutable) {
    auto exp = &unary_exp->exp;
    Type operand;
    switch (unary_exp->op) {
        case UnaryOp::PLUS:
        case UnaryOp::MINUS: operand = NUM; break;
        case UnaryOp::NOT: operand = BOOL;
    }
    analysis(exp, operand, immutable);
    types[unary_exp] = types[exp];
    checkType(unary_exp, upperbound);
}

void SemanticAST::analysis(const BinaryExp* binary_exp, const Type& upperbound, bool immutable) {
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
    analysis(right, rhs_bound, immutable);
    switch (binary_exp->op) {
        case BinaryOp::INDEX: lhs_bound = adt::Pointer(upperbound, immutable).toBoxed(); break;
        case BinaryOp::AND:
        case BinaryOp::OR: lhs_bound = BOOL; break;
        default: lhs_bound = NUM; break;
    }
    analysis(left, lhs_bound, immutable);
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
        case BinaryOp::INDEX:
            if (types[left].is<adt::Array>()) {
                types[binary_exp] = types[left].as<adt::Array>().elem;
            } else {
                types[binary_exp] = types[left].as<adt::Pointer>().elem;
            }
            types[binary_exp] = types[binary_exp].decay(immutable);
            break;
        default:
            types[binary_exp] = types[left] <= types[right] ? types[right] : types[left];
            break;
    }
    checkType(binary_exp, upperbound);
}

void SemanticAST::analysis(const ExpBox* exp_box, const Type& upperbound, bool immutable) {
    auto exp = exp_box->exp.get();
    analysis(exp, upperbound, immutable);
    types[exp_box] = types[exp];
    checkType(exp_box, upperbound);
}

}  // namespace ast
