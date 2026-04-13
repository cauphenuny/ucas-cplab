#include "backend/ir/ir.hpp"
#include "backend/ir/op.hpp"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/ast/ast.hpp"
#include "irgen.h"
#include "utils/error.hpp"
#include "utils/match.hpp"

#include <unordered_map>
#include <utility>
#include <vector>

namespace ir::gen {

auto Generator::gen(const ast::LVal* lval) -> NamedValue {
    auto def = this->ir_defs.at(this->info->definition_of(lval));
    auto type = this->info->type_of(lval);
    if (auto* alloc_ptr = std::get_if<const Alloc*>(&def); alloc_ptr && (*alloc_ptr)->reference) {
        type = (*alloc_ptr)->type.borrow((*alloc_ptr)->immutable);
    }
    return {type, def};
}

auto Generator::gen(const ast::LValExp* lval, Func* func, Block* scope) -> LeftValue {
    return match(
        lval->val,
        [&](const ast::LVal& val) -> LeftValue {
            auto named = gen(&val);
            if (named.type.isPointer()) {
                auto elem_type = named.type.as<ir::type::Reference>().elem;
                auto temp = func->newTemp(elem_type, scope);
                scope->add(UnaryInst{UnaryInstOp::LOAD, temp, Value{LeftValue{named}}});
                return temp;
            }
            return named;
        },
        [&](const ast::BinaryExp& exp) -> LeftValue {
            auto result = gen(&exp, func, scope);
            return match(
                result, [&](const LeftValue& t) -> LeftValue { return t; },
                [&](const ConstexprValue&) -> LeftValue {
                    throw COMPILER_ERROR(fmt::format("invalid expr `{}` in LValExp", exp));
                });
        });
}

auto Generator::gen(const ast::BinaryExp* exp, Func* func, Block* scope) -> Value {
    auto left = gen(&exp->left, func, scope);
    auto right = gen(&exp->right, func, scope);
    auto result = func->newTemp(this->info->type_of(exp), scope);
    scope->add(BinaryInst{convert_op(exp->op), result, std::move(left), std::move(right)});
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
            auto result = func->newTemp(this->info->type_of(&unary_exp), scope);
            scope->add(UnaryInst{convert_op(unary_exp.op), result, std::move(operand)});
            return LeftValue{result};
        },
        [&](const ast::BinaryExp& binary_exp) -> Value { return gen(&binary_exp, func, scope); },
        [&](const ast::CallExp& call_exp) -> Value {
            auto args = std::vector<Value>{};
            for (auto& arg : call_exp.args) {
                args.push_back(gen(&arg, func, scope));
            }
            auto result = func->newTemp(this->info->type_of(&call_exp), scope);
            scope->add(
                CallInst{.result = result, .func = gen(&call_exp.func), .args = std::move(args)});
            return LeftValue{result};
        });
}

}  // namespace ir::gen