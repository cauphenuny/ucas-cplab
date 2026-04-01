#include "semantic_ast.h"

namespace ast {

void SemanticAST::analysis(const FuncParam* param) {
    registerSymbol(param);
    types[param] = calcType(param);
}

void SemanticAST::analysis(const FuncParams* params) {
    for (const auto& param : *params) {
        analysis(&param);
    }
    types[params] = calcType(params).toBoxed();
}

void SemanticAST::analysis(const FuncArgs* args) {
    for (const auto& arg : *args) {
        analysis(&arg);
    }
    types[args] = calcType(args).toBoxed();
}

void SemanticAST::analysis(const FuncDef* func_def, bool is_builtin) {
    auto func_type = calcType(func_def);
    types[func_def] = calcType(func_def).toBoxed();
    registerSymbol(func_def);
    if (is_builtin) return;
    pushScope();
    analysis(&func_def->params);
    analysis(&func_def->block);
    popScope();
    auto block_type = stmt_types[&func_def->block];
    if (!block_type.always_return && !(types[func_def].as<adt::Func>().ret <= VOID)) {
        throw SemanticError(
            func_def->loc,
            fmt::format("non-void function '{}' may not return on all paths", func_def->name));
    }
    auto ret_type = types[func_def].as<adt::Func>().ret;
    if (!(block_type.ret_type <= ret_type)) {
        throw SemanticError(func_def->loc,
                            fmt::format("function '{}' has return type `{}`, but declared as `{}`",
                                        func_def->name, block_type.ret_type, ret_type));
    }
}

}  // namespace ast