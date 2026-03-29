#pragma once

#include "frontend/ast/type.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utils/serialize.hpp>
#include <utils/traits.hpp>
#include <variant>
#include <vector>

namespace ast {

struct CompUnit;
struct ConstDecl;
struct VarDecl;
struct ConstDef;
struct VarDef;
struct ConstInitVal;

using Decl = std::variant<ConstDecl, VarDecl>;

using ConstExp = std::variant<int, float, double, bool>;
struct LValExp;
struct PrimaryExp;
struct UnaryExp;
struct CallExp;
struct BinaryExp;
using Exp = std::variant<PrimaryExp, UnaryExp, CallExp, BinaryExp>;
struct ExpBox; // for recursive exp

struct IfStmt;
struct WhileStmt;
struct ReturnStmt;
struct BreakStmt;
struct ContinueStmt;
struct AssignStmt;
struct BlockStmt;
using Stmt =
    std::variant<IfStmt, WhileStmt, ReturnStmt, BreakStmt, ContinueStmt, AssignStmt, BlockStmt>;
struct StmtBox; // for recursive stmt

struct FuncDef;
struct FuncParam;
using FuncParams = std::vector<FuncParam>;
using FuncArgs = std::vector<Exp>;

struct ConstInitVal : public mixin::Locatable {
    std::variant<ConstExp, std::vector<ConstInitVal>> val;
    DELEGATED_TO_STRING(ConstInitVal, val);
};

struct ConstDecl : public mixin::Locatable {
    Type type;
    std::vector<ConstDef> defs;
    TO_STRING(ConstDecl, type, defs);
};

struct VarDecl : public mixin::Locatable {
    Type type;
    std::vector<VarDef> defs;
    TO_STRING(VarDecl, type, defs);
};

struct ConstDef : public mixin::Locatable {
    std::string name;
    std::vector<std::optional<size_t>> dims;
    ConstInitVal val;
    TO_STRING(ConstDef, name, dims, val);
};

struct VarDef : public mixin::Locatable {
    std::string name;
    std::vector<std::optional<size_t>> dims;
    std::optional<ConstInitVal> val;
    TO_STRING(VarDef, name, dims, val);
};

struct FuncParam : public mixin::Locatable {
    Type type;
    std::string name;
    std::vector<std::optional<size_t>> dims;
    TO_STRING(FuncParam, type, name, dims);
};

struct ExpBox : public mixin::Locatable {
    std::unique_ptr<Exp> exp;

    ExpBox(std::unique_ptr<Exp> exp);
    [[nodiscard]] auto toString() const -> std::string;
    auto toBoxed() && {
        return std::move(exp);
    }
};

struct BinaryExp : public mixin::Locatable, mixin::ToBoxed<BinaryExp, Exp> {
    BinaryOp op;
    ExpBox left, right;
    TO_STRING(BinaryExp, op, left, right);
};

struct LValID : public mixin::Locatable {
    std::string name;
    DELEGATED_TO_STRING(LValID, name);
};

struct LValExp : public mixin::Locatable {
    std::variant<LValID, BinaryExp> val;
    DELEGATED_TO_STRING(LVal, val);
};

struct PrimaryExp : public mixin::Locatable, mixin::ToBoxed<PrimaryExp, Exp> {
private:
    using T = std::variant<ExpBox, LValExp, ConstExp>;

public:
    T exp;
    PrimaryExp(T exp) : exp(std::move(exp)) {}
    DELEGATED_TO_STRING(PrimaryExp, exp);
};

struct UnaryExp : public mixin::Locatable, mixin::ToBoxed<UnaryExp, Exp> {
    UnaryOp op;
    ExpBox exp;
    TO_STRING(UnaryExp, op, exp);
};

struct CallExp : public mixin::Locatable, mixin::ToBoxed<CallExp, Exp> {
    LValID func;
    FuncArgs args;
    TO_STRING(CallExp, func, args);
};

ExpBox::ExpBox(std::unique_ptr<Exp> exp) : exp(std::move(exp)) {
    this->loc = match(*this->exp, [](const auto& subexp) { return subexp.loc; });
}
auto ExpBox::toString() const -> std::string {
    return fmt::format("ExpBox: {}", *exp);
}

struct StmtBox : public mixin::Locatable {
    std::unique_ptr<Stmt> stmt;

    StmtBox(std::unique_ptr<Stmt> stmt);
    auto toBoxed() && {
        return std::move(stmt);
    }
    [[nodiscard]] auto toString() const -> std::string;
};

struct IfStmt : public mixin::Locatable, mixin::ToBoxed<IfStmt, Stmt> {
    Exp cond;
    StmtBox stmt;
    std::optional<StmtBox> else_stmt;
    TO_STRING(IfStmt, cond, stmt, else_stmt);
};

struct WhileStmt : public mixin::Locatable, mixin::ToBoxed<WhileStmt, Stmt> {
    Exp cond;
    StmtBox stmt;
    TO_STRING(WhileStmt, cond, stmt);
};

struct ReturnStmt : public mixin::Locatable, mixin::ToBoxed<ReturnStmt, Stmt> {
    std::optional<Exp> exp;
    TO_STRING(ReturnStmt, exp);
};

struct BreakStmt : public mixin::Locatable, mixin::ToBoxed<BreakStmt, Stmt> {
    EMPTY_TO_STRING(BreakStmt);
};

struct ContinueStmt : public mixin::Locatable, mixin::ToBoxed<ContinueStmt, Stmt> {
    EMPTY_TO_STRING(ContinueStmt);
};

struct AssignStmt : public mixin::Locatable, mixin::ToBoxed<AssignStmt, Stmt> {
    LValExp var;
    Exp exp;
    TO_STRING(AssignStmt, var, exp);
};

struct BlockStmt : public mixin::Locatable, mixin::ToBoxed<BlockStmt, Stmt> {
    using Item = std::variant<Decl, Stmt>;
    std::vector<Item> items;
    DELEGATED_TO_STRING(BlockStmt, items);
};

StmtBox::StmtBox(std::unique_ptr<Stmt> stmt) : stmt(std::move(stmt)) {
    this->loc = match(*this->stmt, [](const auto& substmt) { return substmt.loc; });
}
auto StmtBox::toString() const -> std::string {
    return fmt::format("StmtBox: {}", *stmt);
}

struct FuncDef : public mixin::Locatable {
    Type type;
    std::string name;
    FuncParams params;
    BlockStmt block;
    TO_STRING(FuncDef, type, name, params, block);
};

struct CompUnit : public mixin::Locatable {
    using Item = std::variant<Decl, FuncDef>;
    std::vector<Item> items;
    DELEGATED_TO_STRING(CompUnit, items);
};

}  // namespace ast