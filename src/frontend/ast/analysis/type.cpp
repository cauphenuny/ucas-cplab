#include "backend/ir/type.hpp"

#include "frontend/ast/ast.hpp"
#include "frontend/ast/op.hpp"
#include "semantic_ast.h"

#include <cstddef>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ast {

ir::type::TypeBox SemanticAST::calcType(ast::Type type) {
    switch (type) {
        case ast::Type::INT: return ir::type::construct<int>();
        case ast::Type::FLOAT: return ir::type::construct<float>();
        case ast::Type::BOOL: return ir::type::construct<bool>();
        case ast::Type::DOUBLE: return ir::type::construct<double>();
        case ast::Type::VOID: return ir::type::construct<void>();
    }
}

ir::type::TypeBox SemanticAST::calcType(const FuncParam* param) {
    auto param_type = calcType(param->type);
    if (param->dims.size()) {
        for (size_t i = param->dims.size(); i > 1; i--) {
            param_type =
                ir::type::Array(std::move(param_type), param->dims[i - 1].value()).toBoxed();
        }
        param_type = ir::type::Reference::slice(std::move(param_type))
                         .toBoxed();  // NOTE: degrade sized array to
                                      // reference for function parameters
    }
    return param_type;
}

ir::type::Product SemanticAST::calcType(const FuncArgs* args) {
    auto args_type = ir::type::Product{};
    for (const auto& arg : *args) {
        args_type.append(types[&arg]);
    }
    return args_type;
}

ir::type::Product SemanticAST::calcType(const FuncParams* params) {
    for (auto& param : *params) {
        types[&param] = calcType(&param);
    }
    auto type = ir::type::Product{};
    for (auto& param : *params) {
        type.append(types[&param]);
    }
    return type;
}

ir::type::Func SemanticAST::calcType(const FuncDef* func_def) {
    auto param_types = calcType(&func_def->params);
    auto ret_type = calcType(func_def->type);
    return {std::move(param_types).toBoxed(), std::move(ret_type)};
}

}  // namespace ast