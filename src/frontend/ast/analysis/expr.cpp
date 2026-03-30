#include "semantic_ast.h"
#include "utils/error.hpp"

namespace ast {

void SemanticAST::analysis(const LVal* lid, const Type& upperbound) {
    if (!upperbound.is<adt::Func>()) {
        auto symdef = lookup(lid->name, vars);
        if (!symdef) {
            throw SemanticError(lid->loc, fmt::format("undefined variable '{}'", lid->name));
        }
        match(*symdef, [&](const auto& def) { types[lid] = types[def]; });
    } else {
        auto funcdef = lookup(lid->name, funcs);
        if (!funcdef) {
            throw SemanticError(lid->loc, fmt::format("undefined function '{}'", lid->name));
        }
        types[lid] = types[*funcdef];
    }
    checkType(lid, upperbound);
}

void SemanticAST::analysis(const LValExp* lval, const Type& upperbound) {
    match(lval->val, [&](const auto& subexp) {
        analysis(&subexp, upperbound);
        types[lval] = types[&subexp];
    });
}

void SemanticAST::analysis(const Exp* exp, const Type& upperbound) {
    match(*exp, [&](const auto& subexp) {
        analysis(&subexp, upperbound);
        types[exp] = types[&subexp];
    });
}

void SemanticAST::analysis(const TupleExp* tuple, const Type& upperbound) {
    for (const auto& element : tuple->elements) {
        analysis(&element, ANY);
    }
    types[tuple] = calcType(tuple).toBoxed();
    checkType(tuple, upperbound);
}

void SemanticAST::analysis(const ConstExp* const_exp, const Type& upperbound) {
    types[const_exp] =
        match(*const_exp, [&](auto val) { return adt::construct<const decltype(val)>(); });
}

void SemanticAST::analysis(const PrimaryExp* primary, const Type& upperbound) {
    match(primary->exp, [&](const auto& subexp) {
        analysis(&subexp, upperbound);
        types[primary] = types[&subexp];
    });
    checkType(primary, upperbound);
}

void SemanticAST::analysis(const UnaryExp* unary_exp, const Type& upperbound) {
    auto exp = &unary_exp->exp;
    Type operand;
    switch (unary_exp->op) {
        case UnaryOp::PLUS:
        case UnaryOp::MINUS: operand = NUM; break;
        case UnaryOp::NOT: operand = BOOL;
    }
    analysis(exp, operand);
    types[unary_exp] = types[exp];
    checkType(unary_exp, upperbound);
}

void SemanticAST::analysis(const BinaryExp* binary_exp, const Type& upperbound) {
    using namespace ast;
    auto left = &binary_exp->left;
    auto right = &binary_exp->right;
    Type lhs_bound, rhs_bound;
    switch (binary_exp->op) {
        case BinaryOp::AND:
        case BinaryOp::OR: rhs_bound = BOOL; break;
        case BinaryOp::INDEX: rhs_bound = INT; break;
        case BinaryOp::CALL: rhs_bound = ANY; break;
        default: rhs_bound = NUM; break;
    }
    analysis(right, rhs_bound);
    switch (binary_exp->op) {
        case BinaryOp::CALL:
            lhs_bound = adt::Func(types[right].as<adt::Product>(), ANY).toBoxed();
            break;
        case BinaryOp::INDEX: lhs_bound = ANY; break;
        case BinaryOp::AND:
        case BinaryOp::OR: lhs_bound = BOOL; break;
        default: rhs_bound = NUM; break;
    }
    analysis(left, lhs_bound);
    if (binary_exp->op != BinaryOp::INDEX && binary_exp->op != BinaryOp::CALL) {
        if (!(types[left] <= types[right]) && !(types[right] <= types[left])) {
            throw SemanticError(
                binary_exp->loc,
                fmt::format("type error: cannot apply operator `{}` to `{}` and `{}`",
                            binary_exp->op, types[left], types[right]));
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
            checkType(left, adt::Slice(ANY).toBoxed());
            types[binary_exp] = types[left].as<adt::Slice>().elem;
            break;
        case BinaryOp::CALL: types[binary_exp] = types[left].as<adt::Func>().ret; break;
        default:
            types[binary_exp] = types[left] <= types[right] ? types[right] : types[left];
            break;
    }
    checkType(binary_exp, upperbound);
}

void SemanticAST::analysis(const ExpBox* exp_box, const Type& upperbound) {
    auto exp = exp_box->exp.get();
    analysis(exp, upperbound);
    types[exp_box] = types[exp];
    checkType(exp_box, upperbound);
}

}  // namespace ast
