#include "semantic_ast.h"
#include "utils/error.hpp"

namespace ast {

void SemanticAST::pushScope() {
    vars.emplace_back();
    funcs.emplace_back();
}
void SemanticAST::popScope() {
    vars.pop_back();
    funcs.pop_back();
}

void SemanticAST::registerSymbol(SymDefNode def) {
    match(
        def,
        [&](VarDefNode subdef) {
            match(subdef, [&](const auto& subdef) {
                if (vars.back().count(subdef->name)) {
                    throw SemanticError(subdef->loc,
                                        fmt::format("redefinition of variable '{}' at depth {}",
                                                    subdef->name, vars.size() - 1));
                }
                vars.back()[subdef->name] = subdef;
            });
        },
        [&](FuncDefNode subdef) {
            if (funcs.back().count(subdef->name)) {
                throw SemanticError(subdef->loc,
                                    fmt::format("redefinition of function '{}'", subdef->name));
            }
            funcs.back()[subdef->name] = subdef;
        });
}

}  // namespace ast