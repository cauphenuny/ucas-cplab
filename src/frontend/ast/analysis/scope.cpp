#include "semantic_ast.h"
#include "utils/error.hpp"

namespace ast {

void SemanticAST::pushScope() {
    vars.emplace_back();
}
void SemanticAST::popScope() {
    vars.pop_back();
}

void SemanticAST::registerFunction(FuncDefNode def, bool is_builtin) {
    if (funcs.count(def->name)) {
        throw SemanticError(def->loc, fmt::format("redefinition of function '{}'", def->name));
    }
    funcs[def->name] = {def, is_builtin};
}

void SemanticAST::registerVariable(VarDefNode def) {
    match(def, [&](const auto& subdef) {
        if (vars.back().count(subdef->name)) {
            throw SemanticError(subdef->loc,
                                fmt::format("redefinition of variable '{}' at depth {}",
                                            subdef->name, vars.size() - 1));
        }
        vars.back()[subdef->name] = subdef;
    });
}

}  // namespace ast