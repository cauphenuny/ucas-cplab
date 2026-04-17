#include "backend/ir/ir.hpp"
#include "backend/ir/op.hpp"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/op.hpp"
#include "irgen.h"
#include "utils/match.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace ir::gen {

auto Generator::branch(const ast::Exp* cond, Func* func, Block* scope, Block* true_block,
                       Block* false_block) -> BranchExit {
    return match(
        *cond,
        [&](const ast::BinaryExp& binary_exp) -> BranchExit {
            switch (binary_exp.op) {
                case ast::BinaryOp::AND: {
                    auto right_block = func->newBlock();
                    auto left_exit =
                        branch(binary_exp.left.exp.get(), func, scope, right_block, false_block);
                    right_block->setExit(branch(binary_exp.right.exp.get(), func, right_block,
                                                true_block, false_block));
                    return left_exit;
                }
                case ast::BinaryOp::OR: {
                    auto right_block = func->newBlock();
                    auto left_exit =
                        branch(binary_exp.left.exp.get(), func, scope, true_block, right_block);
                    right_block->setExit(branch(binary_exp.right.exp.get(), func, right_block,
                                                true_block, false_block));
                    return left_exit;
                }
                default: {
                    auto cond_val = gen(cond, func, scope);
                    return BranchExit{std::move(cond_val), true_block, false_block};
                }
            }
        },
        [&](const auto& exp) {
            auto cond_val = gen(cond, func, scope);
            return BranchExit{std::move(cond_val), true_block, false_block};
        });
}

auto Generator::gen(const ast::BlockStmt* block_stmt, Func* func, Block* scope) -> Block* {
    for (const auto& stmt : block_stmt->items) {
        match(
            stmt,
            [&](const ast::Decl& decl) {
                for (auto& alloc : gen(&decl)) {
                    if (alloc->comptime) {
                        func->addLocal(std::move(alloc));
                    } else {
                        auto init = std::move(alloc->init);
                        alloc->init = std::nullopt;
                        auto val = alloc->value();
                        func->addLocal(std::move(alloc));
                        // Local declaration initializers must execute when the declaration runs.
                        if (init) {
                            scope->add(UnaryInst{UnaryInstOp::MOV, val, Value{std::move(*init)}});
                        }
                    }
                }
            },
            [&](const ast::Stmt& stmt) { scope = gen(&stmt, func, scope); });
        if (!scope) {
            break;
        }
    }
    return scope;
}

auto Generator::gen(const ast::Stmt* stmt, Func* func, Block* scope) -> Block* {
    return match(
        *stmt,
        [&](const ast::IfStmt& if_stmt) {
            auto true_block = func->newBlock(fmt::format("if_true_{}", if_stmt.loc));
            auto always_return = this->info->type_of(&if_stmt).always_return;
            auto exit_block =
                always_return ? nullptr : func->newBlock(fmt::format("if_exit_{}", if_stmt.loc));
            if (if_stmt.else_stmt) {
                auto false_block = func->newBlock(fmt::format("if_false_{}", if_stmt.loc));
                scope->setExit(branch(&if_stmt.cond, func, scope, true_block, false_block));
                true_block = gen(&if_stmt.stmt, func, true_block);
                if (true_block) true_block->setExit(JumpExit{exit_block});  // not return
                false_block = gen(&*if_stmt.else_stmt, func, false_block);
                if (false_block) false_block->setExit(JumpExit{exit_block});
                return exit_block;
            } else {
                scope->setExit(branch(&if_stmt.cond, func, scope, true_block, exit_block));
                true_block = gen(&if_stmt.stmt, func, true_block);
                if (true_block) true_block->setExit(JumpExit{exit_block});  // not return
                return exit_block;
            }
        },
        [&](const ast::WhileStmt& while_stmt) {
            auto cond_block = func->newBlock(fmt::format("while_cond_{}", while_stmt.loc));
            auto body_block = func->newBlock(fmt::format("while_body_{}", while_stmt.loc));
            auto exit_block = func->newBlock(fmt::format("while_exit_{}", while_stmt.loc));
            func->pushLoop(cond_block, exit_block);
            scope->setExit(JumpExit{cond_block});
            cond_block->setExit(branch(&while_stmt.cond, func, cond_block, body_block, exit_block));
            body_block = gen(&while_stmt.stmt, func, body_block);
            body_block->setExit(JumpExit{cond_block});
            func->popLoop();
            return exit_block;
        },
        [&](const ast::BlockStmt& block_stmt) { return gen(&block_stmt, func, scope); },
        [&](const ast::AssignStmt& assign_stmt) {
            return match(
                assign_stmt.var.val,
                [&](const ast::LVal& lval) {
                    auto var = gen(&lval);
                    auto exp = gen(&assign_stmt.exp, func, scope);
                    if (var.type.isPointer()) {
                        scope->add(UnaryInst{UnaryInstOp::STORE, std::move(var), std::move(exp)});
                    } else {
                        scope->add(UnaryInst{UnaryInstOp::MOV, std::move(var), std::move(exp)});
                    }
                    return scope;
                },
                [&](const ast::BinaryExp& exp) {  // array indexing
                    auto array = gen(&exp.left, func, scope);
                    auto index = gen(&exp.right, func, scope);
                    auto exp_val = gen(&assign_stmt.exp, func, scope);
                    scope->add(BinaryInst{InstOp::STORE, as_lvalue(array), std::move(index),
                                          std::move(exp_val)});
                    return scope;
                });
        },
        [&](const ast::ExpStmt& exp_stmt) {
            auto _ = gen(&exp_stmt.exp, func, scope);  // discard the result
            return scope;
        },
        [&](const ast::ReturnStmt& return_stmt) -> Block* {
            scope->setExit(ReturnExit{return_stmt.exp ? gen(&*return_stmt.exp, func, scope)
                                                      : Value{ConstexprValue{}}});
            return nullptr;
        },
        [&](const ast::ContinueStmt&) -> Block* {
            scope->setExit(JumpExit{func->currentLoop().continue_target});
            return nullptr;
        },
        [&](const ast::BreakStmt&) -> Block* {
            scope->setExit(JumpExit{func->currentLoop().break_target});
            return nullptr;
        });
}

}  // namespace ir::gen