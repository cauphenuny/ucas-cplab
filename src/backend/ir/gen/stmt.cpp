#include "backend/ir/ir.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/op.hpp"
#include "irgen.h"
#include "utils/error.hpp"

namespace ir::gen {

auto Generator::branch(const ast::Exp* cond, Func* func, Block* scope, const Block* true_block,
                       const Block* false_block) -> BranchExit {
    return match(*cond, [&](const ast::BinaryExp& binary_exp) -> BranchExit {
        switch (binary_exp.op) {
            case ast::BinaryOp::AND: {
                auto right_block = func->newBlock();
                auto left_exit = branch(binary_exp.left.exp.get(), func, scope, right_block, false_block);
                right_block->exit = branch(binary_exp.right.exp.get(), func, right_block, true_block, false_block);
                return left_exit;
            }
            case ast::BinaryOp::OR: {
                auto right_block = func->newBlock();
                auto left_exit = branch(binary_exp.left.exp.get(), func, scope, true_block, right_block);
                right_block->exit = branch(binary_exp.right.exp.get(), func, right_block, true_block, false_block);
                return left_exit;
            }
            default:
                throw CompilerError(fmt::format("invalid condition expression `{}` for branch", binary_exp));
        }
        },
        [&](const auto& exp) {
            auto cond_val = gen(cond, func, scope);
            return BranchExit{cond_val, true_block, false_block};
        });
}

auto Generator::gen(const ast::BlockStmt* block_stmt, Func* func, Block* scope) -> Block* {
    auto current_scope = scope;
    for (const auto& stmt : block_stmt->items) {
        match(
            stmt,
            [&](const ast::Decl& decl) {
                for (const auto& alloc : gen(&decl)) {
                    func->addAlloc(alloc);
                }
            },
            [&](const ast::Stmt& stmt) { current_scope = gen(&stmt, func, current_scope); });
        if (!current_scope) {
            break;
        }
    }
    return current_scope;
}

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
        [&](const ast::BlockStmt& block_stmt) { return gen(&block_stmt, func, scope); },
        [&](const ast::AssignStmt& assign_stmt) {
            auto var = gen(&assign_stmt.var, func, scope);
            auto exp = gen(&assign_stmt.exp, func, scope);
            scope->insts.emplace_back(UnaryInst{UnaryInstOp::MOV, var, exp});
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