#include "backend/ir/ir.hpp"
#include "backend/ir/op.hpp"
#include "frontend/ast/ast.hpp"
#include "irgen.h"

namespace ir::gen {

auto Generator::gen(const ast::LVal* lval) -> LeftValue {
    return NamedValue{this->info->type_of(lval).decay(), this->info->definition_of(lval)};
}

auto Generator::gen(const ast::LValExp* lval, Func* func, Block* scope) -> LeftValue {
    return match(
        lval->val, [&](const ast::LVal& val) -> LeftValue { return gen(&val); },
        [&](const ast::BinaryExp& exp) -> LeftValue {
            auto result = gen(&exp, func, scope);
            return match(
                result, [&](const LeftValue& t) -> LeftValue { return t; },
                [&](const ConstexprValue&) -> LeftValue {
                    throw CompilerError(fmt::format("invalid expr `{}` in LValExp", exp));
                });
        });
}

auto Generator::gen(const ast::BinaryExp* exp, Func* func, Block* scope) -> Value {
    auto left = gen(&exp->left, func, scope);
    auto right = gen(&exp->right, func, scope);
    auto result = func->newTemp(this->info->type_of(exp).decay());
    scope->add(BinaryInst{convert_op(exp->op), result, left, right});
    return LeftValue{result};
}

auto Generator::gen(const ast::ConstExp* exp, Func* func, Block* scope) -> Value {
    return match(*exp, [&](auto val) { return ConstexprValue(val); });
}

auto Generator::gen(const ast::Exp* exp, Func* func, Block* scope) -> Value {
    return match(
        *exp,
        [&](const ast::PrimaryExp& primary) {
            return match(primary.exp,
                         [&](const auto& subexp) -> Value { return gen(&subexp, func, scope); });
        },
        [&](const ast::UnaryExp& unary_exp) -> Value {
            auto operand = gen(&unary_exp.exp, func, scope);
            auto result = func->newTemp(this->info->type_of(&unary_exp).decay());
            scope->add(UnaryInst{convert_op(unary_exp.op), result, operand});
            return LeftValue{result};
        },
        [&](const ast::BinaryExp& binary_exp) -> Value { return gen(&binary_exp, func, scope); },
        [&](const ast::CallExp& call_exp) -> Value {
            auto result = func->newTemp(this->info->type_of(&call_exp).decay());
            auto inst = CallInst{
                .result = result,
                .func = gen(&call_exp.func),
                .args = {},
            };
            for (auto& arg : call_exp.args) {
                inst.args.push_back(gen(&arg, func, scope));
            }
            scope->add(std::move(inst));
            return LeftValue{result};
        });
}

}  // namespace ir::gen