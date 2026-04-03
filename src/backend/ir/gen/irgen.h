#pragma once

#include "backend/ir/ir.hpp"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/ast/ast.hpp"

namespace ir::gen {

struct Generator {
    Program generate(const ast::SemanticAST& info);

private:
    const ast::SemanticAST* info;
    const ast::CompUnit* ast;
    std::unordered_map<ast::SymDefNode, ir::NameDef> ir_defs;

    [[nodiscard]] auto name_of(ast::SymDefNode def) -> std::string {
        return match(
            def, [&](const ast::FuncDef* func) { return func->name; },
            [&](const auto* var) { return fmt::format("{}_{}", var->name, var->loc); });
    }

    [[nodiscard]] auto gen(const ast::ConstInitVal* init, Type target_type) -> ConstexprValue;
    [[nodiscard]] auto gen(const ast::VarDef* def) -> std::unique_ptr<Alloc>;
    [[nodiscard]] auto gen(const ast::ConstDef* def) -> std::unique_ptr<Alloc>;
    [[nodiscard]] auto gen(const ast::FuncParam* param) -> std::unique_ptr<Alloc>;
    [[nodiscard]] auto gen(const ast::Decl* decl) -> std::vector<std::unique_ptr<Alloc>>;

    [[nodiscard]] auto gen(const ast::FuncDef* func) -> std::unique_ptr<Func>;

    [[nodiscard]] auto gen(const ast::BlockStmt* block_stmt, Func* func, Block* scope) -> Block*;
    [[nodiscard]] auto gen(const ast::Stmt* stmt, Func* func, Block* scope) -> Block*;
    [[nodiscard]] auto gen(const ast::StmtBox* stmt_box, Func* func, Block* scope) -> Block* {
        return gen(stmt_box->stmt.get(), func, scope);
    }

    [[nodiscard]] auto gen(const ast::LVal* lval) -> NamedValue;
    [[nodiscard]] auto gen(const ast::LValExp* exp, Func* func, Block* scope) -> LeftValue;
    [[nodiscard]] auto gen(const ast::BinaryExp* exp, Func* func, Block* scope) -> Value;
    [[nodiscard]] auto gen(const ast::ConstExp* exp, Func* func, Block* scope) -> Value;
    [[nodiscard]] auto gen(const ast::Exp* exp, Func* func, Block* scope) -> Value;
    [[nodiscard]] auto gen(const ast::ExpBox* exp_box, Func* func, Block* scope) -> Value {
        return gen(exp_box->exp.get(), func, scope);
    }

    [[nodiscard]] auto branch(const ast::Exp* cond, Func* func, Block* scope,
                              const Block* true_block, const Block* false_block) -> BranchExit;
};

inline auto generate(const ast::SemanticAST& info) -> Program {
    return Generator().generate(info);
}

}  // namespace ir::gen