#include "backend/ir/ir.hpp"
#include "frontend/ast/ast.hpp"
#include "irgen.h"

namespace ir::gen {

auto Generator::gen(const ast::Stmt* stmt, Func* func, Block* scope) -> Block* {
    return match(
        *stmt,
        [&](const ast::IfStmt& if_stmt) {
            auto true_block = func->newBlock(fmt::format(".if_true_{}", if_stmt.loc));
            auto exit_block = func->newBlock(fmt::format(".if_exit_{}", if_stmt.loc));
            if (if_stmt.else_stmt) {
                auto false_block = func->newBlock(fmt::format(".if_false_{}", if_stmt.loc));
                scope->exit = branch(&if_stmt.cond, func, scope, true_block, false_block);
                true_block = gen(&if_stmt.stmt, func, true_block);
                true_block->exit = JumpExit{exit_block};
                false_block = gen(&*if_stmt.else_stmt, func, false_block);
                false_block->exit = JumpExit{exit_block};
                return exit_block;
            } else {
                scope->exit = branch(&if_stmt.cond, func, scope, true_block, exit_block);
                true_block = gen(&if_stmt.stmt, func, true_block);
                true_block->exit = JumpExit{exit_block};
                return exit_block;
            }
        },
        [&](const ast::WhileStmt& while_stmt) {
            auto cond_block = func->newBlock(fmt::format(".while_cond_{}", while_stmt.loc));
            auto body_block = func->newBlock(fmt::format(".while_body_{}", while_stmt.loc));
            auto exit_block = func->newBlock(fmt::format(".while_exit_{}", while_stmt.loc));
            func->pushLoop(cond_block, exit_block);
            scope->exit = JumpExit{cond_block};
            cond_block->exit = branch(&while_stmt.cond, func, cond_block, body_block, exit_block);
            body_block = gen(&while_stmt.stmt, func, body_block);
            body_block->exit = JumpExit{cond_block};
            func->popLoop();
            return exit_block;
        },
        [&](const ast::BlockStmt& block_stmt) {
            auto current_scope = scope;
            for (const auto& stmt : block_stmt.items) {
                match(
                    stmt,
                    [&](const ast::Decl& decl) {
                        for (const auto& alloc : gen(&decl)) {
                            func->addAlloc(alloc);
                        }
                    },
                    [&](const ast::Stmt& stmt) {
                        current_scope = gen(&stmt, func, current_scope);
                    });
                if (!current_scope) {
                    break;
                }
            }
            return current_scope;
        },
        [&](const ast::AssignStmt& assign_stmt) {
            auto var = gen(&assign_stmt.var, func, scope);
            auto exp = gen(&assign_stmt.exp, func, scope);
            scope->insts.emplace_back(RegularInst{InstOp::MOV, var, {}, exp});
            return scope;
        },
        [&](const ast::ExpStmt& exp_stmt) {
            auto _ = gen(&exp_stmt.exp, func, scope);  // discard the result
            return scope;
        },
        [&](const ast::ReturnStmt& return_stmt) -> Block* {
            scope->exit = ReturnExit{return_stmt.exp ? gen(&*return_stmt.exp, func, scope)
                                                      : Value{ConstexprValue{}}};
            return nullptr;
        },
        [&](const ast::ContinueStmt&) -> Block* {
            scope->exit = JumpExit{func->currentLoop().continue_target};
            return nullptr;
        },
        [&](const ast::BreakStmt&) -> Block* {
            scope->exit = JumpExit{func->currentLoop().break_target};
            return nullptr;
        });
}

}  // namespace ir::gen