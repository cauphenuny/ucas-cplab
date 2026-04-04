#include "semantic_ast.h"

namespace ast {

adt::TypeBox SemanticAST::calcType(ast::Type type) {
    switch (type) {
        case ast::Type::INT: return adt::construct<int>();
        case ast::Type::FLOAT: return adt::construct<float>();
        case ast::Type::BOOL: return adt::construct<bool>();
        case ast::Type::DOUBLE: return adt::construct<double>();
        case ast::Type::VOID: return adt::construct<void>();
    }
}

adt::TypeBox SemanticAST::calcType(const FuncParam* param) {
    auto param_type = calcType(param->type);
    if (param->dims.size()) {
        for (size_t i = param->dims.size(); i > 1; i--) {
            param_type = adt::Array(std::move(param_type), param->dims[i - 1].value()).toBoxed();
        }
        param_type =
            adt::Pointer(std::move(param_type)).toBoxed();  // NOTE: degrade sized array to pointer
                                                            // for function parameters
    }
    return param_type;
}

adt::Product SemanticAST::calcType(const FuncArgs* args) {
    auto args_type = adt::Product{};
    for (const auto& arg : *args) {
        args_type.append(types[&arg]);
    }
    return args_type;
}

adt::Product SemanticAST::calcType(const FuncParams* params) {
    for (auto& param : *params) {
        types[&param] = calcType(&param);
    }
    auto type = adt::Product{};
    for (auto& param : *params) {
        type.append(types[&param]);
    }
    return type;
}

adt::Func SemanticAST::calcType(const FuncDef* func_def) {
    auto param_types = calcType(&func_def->params);
    auto ret_type = calcType(func_def->type);
    return {std::move(param_types).toBoxed(), std::move(ret_type)};
}

}  // namespace ast