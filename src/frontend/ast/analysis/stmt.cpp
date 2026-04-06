#include "frontend/ast/ast.hpp"
#include "semantic_ast.h"
#include "utils/match.hpp"

#include <memory>
#include <optional>
#include <unordered_map>

namespace ast {

void SemanticAST::analysis(const BlockStmt* block) {
    for (const auto& item : block->items) {
        match(
            item, [&](const Decl& subitem) { analysis(&subitem); },
            [&](const Stmt& subitem) {
                analysis(&subitem);
                stmt_types[block].append(stmt_types[&subitem]);
            });
    }
}

void SemanticAST::analysis(const StmtBox* stmt_box) {
    analysis(&*stmt_box->stmt);
    stmt_types[stmt_box] = stmt_types[&*stmt_box->stmt];
}

void SemanticAST::analysis(const Stmt* stmt) {
    match(
        *stmt,
        [&](const BlockStmt& block) {
            pushScope(), analysis(&block), popScope();
            stmt_types[stmt] = stmt_types[&block];
        },
        [&](const auto& substmt) {
            analysis(&substmt);
            stmt_types[stmt] = stmt_types[&substmt];
        });
}

void SemanticAST::analysis(const IfStmt* if_stmt) {
    analysis(&if_stmt->cond, BOOL);
    analysis(&if_stmt->stmt);
    if (if_stmt->else_stmt) {
        auto else_stmt = &*if_stmt->else_stmt;
        analysis(else_stmt);
        stmt_types[if_stmt] = stmt_types[else_stmt];
    }
    stmt_types[if_stmt].merge(stmt_types[&if_stmt->stmt]);
}

void SemanticAST::analysis(const WhileStmt* while_stmt) {
    analysis(&while_stmt->cond, BOOL);
    analysis(&while_stmt->stmt);
    stmt_types[while_stmt].merge(stmt_types[&while_stmt->stmt]);
}

void SemanticAST::analysis(const ReturnStmt* return_stmt) {
    if (return_stmt->exp) {
        auto exp = &*return_stmt->exp;
        analysis(exp, ANY, true);
        stmt_types[return_stmt] = StmtType{.ret_type = types[exp], .always_return = true};
    } else {
        stmt_types[return_stmt] = StmtType{.ret_type = VOID, .always_return = true};
    }
}

void SemanticAST::analysis(const AssignStmt* assign_stmt) {
    auto var = &assign_stmt->var;
    auto exp = &assign_stmt->exp;
    analysis(exp, ANY, true);
    analysis(var, types[exp], false);
}

void SemanticAST::analysis(const ExpStmt* exp_stmt) {
    analysis(&exp_stmt->exp);
}

}  // namespace ast
