#include "semantic_ast.h"

namespace ast {

void SemanticAST::analysis(const FuncParam* param) {
    types[param] = calcType(param);
    registerSymbol(param);
}

void SemanticAST::analysis(const FuncParams* params) {
    types[params] = calcType(params).toBoxed();
    for (const auto& param : *params) {
        analysis(&param);
    }
}

void SemanticAST::analysis(const FuncDef* func_def, bool is_builtin) {
    types[func_def] = calcType(func_def).toBoxed();
    registerSymbol(func_def);
    if (is_builtin) return;
    pushScope();
    analysis(&func_def->params);
    analysis(&func_def->block);
    popScope();
    auto block_type = stmt_types[&func_def->block];
    if (!block_type.always_return) {
        throw SemanticError(
            func_def->loc,
            fmt::format("function '{}' may not return on all paths", func_def->name));
    }
    auto ret_type = types[func_def].as<adt::Func>().ret;
    if (!(block_type.ret_type <= ret_type)) {
        throw SemanticError(
            func_def->loc,
            fmt::format("function '{}' has return type `{}`, but declared as `{}`",
                        func_def->name, block_type.ret_type, ret_type));
    }
}

}