#pragma once

#include "CACTBaseVisitor.h"
#include "frontend/ast/ast.h"

#include <utility>

class ASTVisitor : public CACTBaseVisitor {
public:
    template <typename T> using node_ptr = std::shared_ptr<T>;

    template <typename T> static std::any wrap(T&& val) {
        return std::make_any<node_ptr<T>>(std::make_shared<T>(std::forward<T>(val)));
    }

    template <typename T> static T take(std::any a) {
        if (a.type() == typeid(node_ptr<T>)) {
            return std::move(*std::any_cast<node_ptr<T>>(a));
        }
        throw std::runtime_error(fmt::format("take failed for type {}", typeid(T).name()));
    }

    std::any visitCompUnit(CACTParser::CompUnitContext* ctx) override {
        ast::CompUnit compUnit;
        for (auto* child : ctx->children) {
            auto res = visit(child);
            if (res.has_value()) {
                if (res.type() == typeid(node_ptr<ast::Decl>)) {
                    compUnit.items.emplace_back(take<ast::Decl>(res));
                } else if (res.type() == typeid(node_ptr<ast::FuncDef>)) {
                    compUnit.items.emplace_back(take<ast::FuncDef>(res));
                }
            }
        }
        return wrap(std::move(compUnit));
    }

    std::any visitDecl(CACTParser::DeclContext* ctx) override {
        if (ctx->constDecl()) return visit(ctx->constDecl());
        if (ctx->varDecl()) return visit(ctx->varDecl());
        return {};
    }

    std::any visitConstDecl(CACTParser::ConstDeclContext* ctx) override {
        ast::ConstDecl constDecl;
        constDecl.type = take<ast::Type>(visit(ctx->basicType()));
        for (auto* constDefCtx : ctx->constDef()) {
            constDecl.defs.push_back(take<ast::ConstDef>(visit(constDefCtx)));
        }
        return wrap(ast::Decl(std::move(constDecl)));
    }

    std::any visitVarDecl(CACTParser::VarDeclContext* ctx) override {
        ast::VarDecl varDecl;
        varDecl.type = take<ast::Type>(visit(ctx->basicType()));
        for (auto* varDefCtx : ctx->varDef()) {
            varDecl.defs.push_back(take<ast::VarDef>(visit(varDefCtx)));
        }
        return wrap(ast::Decl(std::move(varDecl)));
    }

    std::any visitConstDef(CACTParser::ConstDefContext* ctx) override {
        ast::ConstDef constDef;
        constDef.name = ctx->ID()->getText();
        for (auto* intLit : ctx->INT_LITERAL()) {
            constDef.dims.push_back(std::stoi(intLit->getText(), nullptr, 0));
        }
        constDef.val = take<ast::ConstInitVal>(visit(ctx->constInitVal()));
        return wrap(std::move(constDef));
    }

    std::any visitVarDef(CACTParser::VarDefContext* ctx) override {
        ast::VarDef varDef;
        varDef.name = ctx->ID()->getText();
        for (auto* intLit : ctx->INT_LITERAL()) {
            varDef.dims.push_back(std::stoi(intLit->getText(), nullptr, 0));
        }
        if (ctx->constInitVal()) {
            varDef.val = take<ast::ConstInitVal>(visit(ctx->constInitVal()));
        }
        return wrap(std::move(varDef));
    }

    std::any visitConstInitVal(CACTParser::ConstInitValContext* ctx) override {
        ast::ConstInitVal initVal;
        if (ctx->constExp()) {
            initVal.val = take<ast::ConstExp>(visit(ctx->constExp()));
        } else {
            std::vector<std::unique_ptr<ast::ConstInitVal>> vals;
            for (auto* child : ctx->constInitVal()) {
                vals.push_back(
                    std::make_unique<ast::ConstInitVal>(take<ast::ConstInitVal>(visit(child))));
            }
            initVal.val = std::move(vals);
        }
        return wrap(std::move(initVal));
    }

    std::any visitFuncDef(CACTParser::FuncDefContext* ctx) override {
        ast::FuncDef funcDef;
        funcDef.type = take<ast::Type>(visit(ctx->funcType()));
        funcDef.name = ctx->ID()->getText();
        if (ctx->funcParams()) {
            funcDef.params = take<ast::FuncParams>(visit(ctx->funcParams()));
        }
        funcDef.block = take<ast::Block>(visit(ctx->block()));
        return wrap(std::move(funcDef));
    }

    std::any visitFuncParams(CACTParser::FuncParamsContext* ctx) override {
        ast::FuncParams params;
        for (auto* paramCtx : ctx->funcParam()) {
            params.push_back(take<ast::FuncParam>(visit(paramCtx)));
        }
        return wrap(std::move(params));
    }

    std::any visitFuncParam(CACTParser::FuncParamContext* ctx) override {
        ast::FuncParam param;
        param.type = take<ast::Type>(visit(ctx->basicType()));
        param.name = ctx->ID()->getText();
        for (auto* intLit : ctx->INT_LITERAL()) {
            param.dims.push_back(std::stoi(intLit->getText(), nullptr, 0));
        }
        if (ctx->getText().find("[]") != std::string::npos) {
            param.dims.insert(param.dims.begin(), -1);
        }
        return wrap(std::move(param));
    }

    std::any visitBlock(CACTParser::BlockContext* ctx) override {
        ast::Block block;
        for (auto* itemCtx : ctx->blockItem()) {
            block.push_back(take<ast::BlockItem>(visit(itemCtx)));
        }
        return wrap(std::move(block));
    }

    std::any visitBlockItem(CACTParser::BlockItemContext* ctx) override {
        if (ctx->decl()) return wrap(ast::BlockItem(take<ast::Decl>(visit(ctx->decl()))));
        if (ctx->stmt()) return wrap(ast::BlockItem(take<ast::StmtBox>(visit(ctx->stmt()))));
        return {};
    }

    std::any visitBasicType(CACTParser::BasicTypeContext* ctx) override {
        if (ctx->INT()) return wrap(ast::Type::INT);
        if (ctx->BOOL()) return wrap(ast::Type::BOOL);
        if (ctx->FLOAT()) return wrap(ast::Type::FLOAT);
        if (ctx->DOUBLE()) return wrap(ast::Type::DOUBLE);
        return {};
    }

    std::any visitFuncType(CACTParser::FuncTypeContext* ctx) override {
        if (ctx->basicType()) return visit(ctx->basicType());
        if (ctx->VOID()) return wrap(ast::Type::VOID);
        return {};
    }

    std::any visitConstExp(CACTParser::ConstExpContext* ctx) override {
        if (ctx->number()) return visit(ctx->number());
        if (ctx->boolNumber()) return visit(ctx->boolNumber());
        return {};
    }

    std::any visitNumber(CACTParser::NumberContext* ctx) override {
        if (ctx->INT_LITERAL())
            return wrap(ast::ConstExp(std::stoi(ctx->INT_LITERAL()->getText(), nullptr, 0)));
        if (ctx->FLOAT_LITERAL())
            return wrap(ast::ConstExp(std::stof(ctx->FLOAT_LITERAL()->getText())));
        if (ctx->DOUBLE_LITERAL())
            return wrap(ast::ConstExp(std::stod(ctx->DOUBLE_LITERAL()->getText())));
        return {};
    }

    std::any visitBoolNumber(CACTParser::BoolNumberContext* ctx) override {
        return wrap(ast::ConstExp(ctx->TRUE() != nullptr));
    }

    std::any visitLVal(CACTParser::LValContext* ctx) override {
        ast::LValExp lval;
        lval.name = ctx->ID()->getText();
        for (auto* expCtx : ctx->exp()) {
            lval.indices.push_back(take<ast::Exp>(visit(expCtx)));
        }
        return wrap(std::move(lval));
    }

    std::any visitStmt(CACTParser::StmtContext* ctx) override {
        if (ctx->lVal()) {
            return wrap(ast::AssignStmt{.var = take<ast::LValExp>(visit(ctx->lVal())),
                                        .exp = take<ast::Exp>(visit(ctx->exp()))}
                            .toBoxed());
        }
        if (ctx->RETURN()) {
            ast::ReturnStmt stmt;
            if (ctx->exp()) {
                stmt.exp = take<ast::Exp>(visit(ctx->exp()));
            }
            return wrap(std::move(stmt).toBoxed());
        }
        if (ctx->IF()) {
            return wrap(ast::IfStmt{
                .cond = take<ast::Exp>(visit(ctx->cond())),
                .stmt = take<ast::StmtBox>(visit(ctx->stmt(0))),
                .else_stmt = (ctx->ELSE()) ? take<ast::StmtBox>(visit(ctx->stmt(1))) : nullptr}
                            .toBoxed());
        }
        if (ctx->WHILE()) {
            return wrap(ast::WhileStmt{.cond = take<ast::Exp>(visit(ctx->cond())),
                                       .stmt = take<ast::StmtBox>(visit(ctx->stmt(0)))}
                            .toBoxed());
        }
        if (ctx->BREAK()) {
            return wrap(ast::BreakStmt{}.toBoxed());
        }
        if (ctx->CONTINUE()) {
            return wrap(ast::ContinueStmt{}.toBoxed());
        }
        if (ctx->block()) {
            return wrap(ast::Stmt(take<ast::Block>(visit(ctx->block()))).toBoxed());
        }
        return wrap(ast::Stmt(ast::Block{}).toBoxed());
    }

    std::any visitExp(CACTParser::ExpContext* ctx) override {
        return visit(ctx->addExp());
    }

    std::any visitCond(CACTParser::CondContext* ctx) override {
        return visit(ctx->lOrExp());
    }

    std::any visitLOrExp(CACTParser::LOrExpContext* ctx) override {
        if (ctx->lOrExp()) {
            ast::BinaryExp bin;
            bin.op = ast::BinaryOp::OR;
            bin.left = std::move(take<ast::Exp>(visit(ctx->lOrExp()))).toBoxed();
            bin.right = std::move(take<ast::Exp>(visit(ctx->lAndExp()))).toBoxed();
            return wrap(ast::Exp(std::move(bin)));
        }
        return visit(ctx->lAndExp());
    }

    std::any visitLAndExp(CACTParser::LAndExpContext* ctx) override {
        if (ctx->lAndExp()) {
            ast::BinaryExp bin;
            bin.op = ast::BinaryOp::AND;
            bin.left = std::move(take<ast::Exp>(visit(ctx->lAndExp()))).toBoxed();
            bin.right = std::move(take<ast::Exp>(visit(ctx->eqExp()))).toBoxed();
            return wrap(ast::Exp(std::move(bin)));
        }
        return visit(ctx->eqExp());
    }

    std::any visitEqExp(CACTParser::EqExpContext* ctx) override {
        if (ctx->eqExp()) {
            ast::BinaryExp bin;
            bin.op = (ctx->getText().find("==") != std::string::npos) ? ast::BinaryOp::EQ
                                                                      : ast::BinaryOp::NEQ;
            bin.left = std::move(take<ast::Exp>(visit(ctx->eqExp()))).toBoxed();
            bin.right = std::move(take<ast::Exp>(visit(ctx->relExp()))).toBoxed();
            return wrap(ast::Exp(std::move(bin)));
        }
        return visit(ctx->relExp());
    }

    std::any visitRelExp(CACTParser::RelExpContext* ctx) override {
        if (ctx->relExp()) {
            ast::BinaryExp bin;
            auto text = ctx->children[1]->getText();
            if (text == "<")
                bin.op = ast::BinaryOp::LT;
            else if (text == ">")
                bin.op = ast::BinaryOp::GT;
            else if (text == "<=")
                bin.op = ast::BinaryOp::LEQ;
            else
                bin.op = ast::BinaryOp::GEQ;
            bin.left = std::move(take<ast::Exp>(visit(ctx->relExp()))).toBoxed();
            bin.right = std::move(take<ast::Exp>(visit(ctx->addExp()))).toBoxed();
            return wrap(ast::Exp(std::move(bin)));
        }
        return visit(ctx->addExp());
    }

    std::any visitAddExp(CACTParser::AddExpContext* ctx) override {
        if (ctx->addExp()) {
            ast::BinaryExp bin;
            bin.op = (ctx->children[1]->getText() == "+") ? ast::BinaryOp::ADD : ast::BinaryOp::SUB;
            bin.left = std::move(take<ast::Exp>(visit(ctx->addExp()))).toBoxed();
            bin.right = std::move(take<ast::Exp>(visit(ctx->mulExp()))).toBoxed();
            return wrap(ast::Exp(std::move(bin)));
        }
        return visit(ctx->mulExp());
    }

    std::any visitMulExp(CACTParser::MulExpContext* ctx) override {
        if (ctx->mulExp()) {
            ast::BinaryExp bin;
            auto text = ctx->children[1]->getText();
            if (text == "*")
                bin.op = ast::BinaryOp::MUL;
            else if (text == "/")
                bin.op = ast::BinaryOp::DIV;
            else
                bin.op = ast::BinaryOp::MOD;
            bin.left = std::move(take<ast::Exp>(visit(ctx->mulExp()))).toBoxed();
            bin.right = std::move(take<ast::Exp>(visit(ctx->unaryExp()))).toBoxed();
            return wrap(ast::Exp(std::move(bin)));
        }
        return visit(ctx->unaryExp());
    }

    std::any visitUnaryExp(CACTParser::UnaryExpContext* ctx) override {
        if (ctx->primaryExp()) return visit(ctx->primaryExp());
        if (ctx->ID()) {
            ast::CallExp call;
            call.name = ctx->ID()->getText();
            if (ctx->funcArgs()) {
                call.args = take<ast::FuncArgs>(visit(ctx->funcArgs()));
            }
            return wrap(ast::Exp(std::move(call)));
        }
        ast::UnaryExp unary;
        auto text = ctx->children[0]->getText();
        if (text == "+")
            unary.op = ast::UnaryOp::PLUS;
        else if (text == "-")
            unary.op = ast::UnaryOp::MINUS;
        else
            unary.op = ast::UnaryOp::NOT;
        unary.exp = std::move(take<ast::Exp>(visit(ctx->unaryExp()))).toBoxed();
        return wrap(ast::Exp(std::move(unary)));
    }

    std::any visitPrimaryExp(CACTParser::PrimaryExpContext* ctx) override {
        if (ctx->exp()) return visit(ctx->exp());
        if (ctx->lVal())
            return wrap(ast::Exp(ast::PrimaryExp(take<ast::LValExp>(visit(ctx->lVal())))));
        if (ctx->number())
            return wrap(ast::Exp(ast::PrimaryExp(take<ast::ConstExp>(visit(ctx->number())))));
        if (ctx->boolNumber())
            return wrap(ast::Exp(ast::PrimaryExp(take<ast::ConstExp>(visit(ctx->boolNumber())))));
        return {};
    }

    std::any visitFuncArgs(CACTParser::FuncArgsContext* ctx) override {
        ast::FuncArgs args;
        for (auto* expCtx : ctx->exp()) {
            args.push_back(take<ast::Exp>(visit(expCtx)));
        }
        return wrap(std::move(args));
    }
};