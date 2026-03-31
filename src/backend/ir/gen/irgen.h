#pragma once

#include "backend/ir/ir.hpp"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/ast/ast.hpp"

namespace ir::gen {

struct Generator {
    static Program generate(const ast::SemanticAST& info);

private:
    const ast::SemanticAST* info;
    const ast::CompUnit* ast;

    [[nodiscard]] auto gen(const ast::VarDef* def) -> Alloc;
    [[nodiscard]] auto gen(const ast::ConstDef* def) -> Alloc;
    [[nodiscard]] auto gen(const ast::Decl* decl) -> std::vector<Alloc>;

    [[nodiscard]] auto gen(const ast::FuncDef* func) -> Func;

    [[nodiscard]] auto gen(const ast::Stmt* stmt, Func* func, Block* scope) -> Block*;
    [[nodiscard]] auto gen(const ast::StmtBox* stmt_box, Func* func, Block* scope) -> Block* {
        return gen(stmt_box->stmt.get(), func, scope);
    }

    [[nodiscard]] auto gen(const ast::LValExp* exp, Func* func, Block* scope) -> LeftValue;
    [[nodiscard]] auto gen(const ast::Exp* exp, Func* func, Block* scope) -> Value;
    [[nodiscard]] auto gen(const ast::ExpBox* exp_box, Func* func, Block* scope) -> Value {
        return gen(exp_box->exp.get(), func, scope);
    }

    [[nodiscard]] auto branch(const ast::Exp* cond, Func* func, Block* scope, const Block* true_block,
                const Block* false_block) -> BranchExit;
};

}  // namespace ir::gen