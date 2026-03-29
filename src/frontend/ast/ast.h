#pragma once

#include "frontend/ast/op.h"

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
struct BinaryExp;
struct TupleExp;
using Exp = std::variant<PrimaryExp, UnaryExp, BinaryExp, TupleExp>;
struct ExpBox;  // for recursive exp

struct IfStmt;
struct WhileStmt;
struct ReturnStmt;
struct BreakStmt;
struct ContinueStmt;
struct AssignStmt;
struct BlockStmt;
struct ExpStmt;
using Stmt = std::variant<IfStmt, WhileStmt, ReturnStmt, BreakStmt, ContinueStmt, AssignStmt,
                          BlockStmt, ExpStmt>;
struct StmtBox;  // for recursive stmt

struct FuncDef;
struct FuncParam;
using FuncParams = std::vector<FuncParam>;

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
    template <typename T> [[nodiscard]] auto is() const -> bool;
    template <typename T> [[nodiscard]] auto as() const -> const T&;
};

struct BinaryExp : public mixin::Locatable, mixin::ToBoxed<BinaryExp, Exp> {
    BinaryOp op;
    ExpBox left, right;
    TO_STRING(BinaryExp, op, left, right);
};

struct LVal : public mixin::Locatable {
    std::string name;
    DELEGATED_TO_STRING(LVal, name);
};

struct LValExp : public mixin::Locatable {
    std::variant<LVal, BinaryExp> val;
    DELEGATED_TO_STRING(LValExp, val);
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

struct TupleExp : public mixin::Locatable, mixin::ToBoxed<TupleExp, Exp> {
    std::vector<Exp> elements;
    DELEGATED_TO_STRING(TupleExp, elements);
};

ExpBox::ExpBox(std::unique_ptr<Exp> exp) : exp(std::move(exp)) {
    this->loc = match(*this->exp, [](const auto& subexp) { return subexp.loc; });
}
auto ExpBox::toString() const -> std::string {
    return fmt::format("ExpBox: {}", *exp);
}
template <typename T> auto ExpBox::is() const -> bool {
    return std::holds_alternative<T>(*exp);
}
template <typename T> auto ExpBox::as() const -> const T& {
    return std::get<T>(*exp);
}

struct StmtBox : public mixin::Locatable {
    std::unique_ptr<Stmt> stmt;

    StmtBox(std::unique_ptr<Stmt> stmt);
    auto toBoxed() && {
        return std::move(stmt);
    }
    [[nodiscard]] auto toString() const -> std::string;
    template <typename T> [[nodiscard]] auto is() const -> bool;
    template <typename T> [[nodiscard]] auto as() const -> const T&;
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

struct ExpStmt : public mixin::Locatable, mixin::ToBoxed<ExpStmt, Stmt> {
    Exp exp;
    TO_STRING(ExpStmt, exp);
};

StmtBox::StmtBox(std::unique_ptr<Stmt> stmt) : stmt(std::move(stmt)) {
    this->loc = match(*this->stmt, [](const auto& substmt) { return substmt.loc; });
}
auto StmtBox::toString() const -> std::string {
    return fmt::format("StmtBox: {}", *stmt);
}
template <typename T> auto StmtBox::is() const -> bool {
    return std::holds_alternative<T>(*stmt);
}
template <typename T> auto StmtBox::as() const -> const T& {
    return std::get<T>(*stmt);
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