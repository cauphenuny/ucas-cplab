#include "semantic_ast.h"

namespace ast {

    adt::TypeBox SemanticAST::calcType(ast::Type type, bool immutable) {
        switch (type) {
            case ast::Type::INT: return adt::Int{.immutable = immutable}.toBoxed();
            case ast::Type::FLOAT: return adt::Float{.immutable = immutable}.toBoxed();
            case ast::Type::BOOL: return adt::Bool{.immutable = immutable}.toBoxed();
            case ast::Type::DOUBLE: return adt::Double{.immutable = immutable}.toBoxed();
            case ast::Type::VOID: return adt::construct<const void>();
        }
    }

    adt::TypeBox SemanticAST::calcType(const FuncParam* param) {
        auto param_type = calcType(param->type);
        if (param->dims.size()) {
            for (size_t i = param->dims.size(); i > 1; i--) {
                param_type = adt::Slice(std::move(param_type), param->dims[i - 1]).toBoxed();
            }
            param_type = adt::Slice(std::move(param_type))
                             .toBoxed();  // NOTE: degrade sized array to unsized array for function
                                          // parameters
        }
        return param_type;
    }

    adt::Product SemanticAST::calcType(const TupleExp* tuple) {
        auto args_type = adt::Product{};
        for (const auto& arg : tuple->elements) {
            args_type.append(types[&arg]);
        }
        return args_type;
    }

    adt::Product SemanticAST::calcType(const FuncParams* params) {
        auto type = adt::Product{};
        for (auto& param : *params) {
            type.append(calcType(&param));
        }
        return type;
    }

    adt::Func SemanticAST::calcType(const FuncDef* func_def) {
        auto param_types = calcType(&func_def->params);
        auto ret_type = calcType(func_def->type);
        return {std::move(param_types), std::move(ret_type)};
    }

}