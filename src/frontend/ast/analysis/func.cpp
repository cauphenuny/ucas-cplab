#include "backend/ir/type.hpp"
#include "frontend/ast/ast.hpp"
#include "semantic_ast.h"
#include "utils/error.hpp"
#include "utils/traits.hpp"

#include <cstddef>
#include <unordered_map>
#include <vector>

namespace ast {

void SemanticAST::analysis(const FuncParam* param) {
    registerVariable(param);
    types[param] = calcType(param);
}

void SemanticAST::analysis(const FuncParams* params) {
    for (const auto& param : *params) {
        analysis(&param);
    }
    types[params] = calcType(params).toBoxed();
}

void SemanticAST::analysis(const FuncArgs* args, const ir::type::Product& param_types) {
    for (size_t i = 0; i < args->size(); i++) {
        auto upperbound = param_types.items().at(i);
        analysis(&args->at(i), upperbound, true);
    }
    types[args] = calcType(args).toBoxed();
}

void SemanticAST::analysis(const FuncDef* func_def, bool is_builtin) {
    auto func_type = calcType(func_def);
    types[func_def] = calcType(func_def).toBoxed();
    registerFunction(func_def, is_builtin);
    if (is_builtin) return;
    pushScope();
    analysis(&func_def->params);
    analysis(&func_def->block);
    popScope();
    auto block_type = stmt_types[&func_def->block];
    if (!block_type.always_return && !(types[func_def].as<ir::type::Func>().ret <= VOID)) {
        throw SemanticError(
            func_def->loc,
            fmt::format("non-void function '{}' may not return on all paths", func_def->name));
    }
    auto ret_type = types[func_def].as<ir::type::Func>().ret;
    if (!(block_type.ret_type <= ret_type)) {
        throw SemanticError(func_def->loc,
                            fmt::format("function '{}' has return type `{}`, but declared as `{}`",
                                        func_def->name, block_type.ret_type, ret_type));
    }
}

}  // namespace ast