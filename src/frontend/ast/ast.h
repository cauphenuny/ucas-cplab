#pragma once

#include "frontend/ast/type.h"
#include <string>
#include <utils/serialize.hpp>
#include <utils/traits.hpp>
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

struct IfStmt;
struct WhileStmt;
struct ReturnStmt;
struct BreakStmt;
struct ContinueStmt;
struct AssignStmt;

struct Exp;
struct Stmt;
using ExpBox = std::unique_ptr<Exp>;    // for recursive exp types
using StmtBox = std::unique_ptr<Stmt>;  // for recursive stmt types
using BlockItem = std::variant<Decl, StmtBox>;
using Block = std::vector<BlockItem>;

struct FuncDef;
struct FuncParam;
using FuncParams = std::vector<FuncParam>;
using FuncArgs = std::vector<Exp>;

struct ConstInitVal {
    std::variant<ConstExp, std::vector<ConstInitVal>> val;
    DELEGATED_TO_STRING(ConstInitVal, val);
};

struct ConstDecl {
    Type type;
    std::vector<ConstDef> defs;
    TO_STRING(ConstDecl, type, defs);
};

struct VarDecl {
    Type type;
    std::vector<VarDef> defs;
    TO_STRING(VarDecl, type, defs);
};

struct ConstDef {
    std::string name;
    std::vector<int> dims;
    ConstInitVal val;
    TO_STRING(ConstDef, name, dims, val);
};

struct VarDef {
    std::string name;
    std::vector<int> dims;
    std::optional<ConstInitVal> val;
    TO_STRING(VarDef, name, dims, val);
};

struct FuncParam {
    Type type;
    std::string name;
    std::vector<int> dims;
    TO_STRING(FuncParam, type, name, dims);
};

struct LValExp {
    std::string name;
    std::vector<Exp> indices;
    TO_STRING(LeftVal, name, indices);
};

struct PrimaryExp : traits::ToBoxed<PrimaryExp, Exp> {
private:
    using T = std::variant<ExpBox, LValExp, ConstExp>;
    T exp;

public:
    PrimaryExp(T exp) : exp(std::move(exp)) {}
    DELEGATED_TO_STRING(PrimaryExp, exp);
};

struct UnaryExp : traits::ToBoxed<UnaryExp, Exp> {
    UnaryOp op;
    ExpBox exp;
    TO_STRING(UnaryExp, op, exp);
};

struct CallExp {
    std::string name;
    FuncArgs args;
    TO_STRING(CallExp, name, args);
};

struct BinaryExp : traits::ToBoxed<BinaryExp, Exp> {
    BinaryOp op;
    ExpBox left, right;
    TO_STRING(BinaryExp, op, left, right);
};

struct Exp : traits::ToBoxed<Exp> {
private:
    using T = std::variant<PrimaryExp, UnaryExp, CallExp, BinaryExp>;
    T exp;

public:
    Exp(T exp) : exp(std::move(exp)) {}
    DELEGATED_TO_STRING(Exp, exp);
};

struct IfStmt : traits::ToBoxed<IfStmt, Stmt> {
    Exp cond;
    StmtBox stmt;
    StmtBox else_stmt;
    TO_STRING(IfStmt, cond, stmt, else_stmt);
};

struct WhileStmt : traits::ToBoxed<WhileStmt, Stmt> {
    Exp cond;
    StmtBox stmt;
    TO_STRING(WhileStmt, cond, stmt);
};

struct ReturnStmt : traits::ToBoxed<ReturnStmt, Stmt> {
    std::optional<Exp> exp;
    TO_STRING(ReturnStmt, exp);
};

struct BreakStmt : traits::ToBoxed<BreakStmt, Stmt> {
    EMPTY_TO_STRING(BreakStmt);
};

struct ContinueStmt : traits::ToBoxed<ContinueStmt, Stmt> {
    EMPTY_TO_STRING(ContinueStmt);
};

struct AssignStmt : traits::ToBoxed<AssignStmt, Stmt> {
    LValExp var;
    Exp exp;
    TO_STRING(AssignStmt, var, exp);
};

struct Stmt : traits::ToBoxed<Stmt> {
private:
    using T =
        std::variant<IfStmt, WhileStmt, ReturnStmt, BreakStmt, ContinueStmt, AssignStmt, Block>;
    T stmt;

public:
    Stmt(T stmt) : stmt(std::move(stmt)) {}
    DELEGATED_TO_STRING(Stmt, stmt);
};

struct FuncDef {
    Type type;
    std::string name;
    FuncParams params;
    Block block;
    TO_STRING(FuncDef, type, name, params, block);
};

struct CompUnit {
    using Item = std::variant<Decl, FuncDef>;
    std::vector<Item> items;
    DELEGATED_TO_STRING(CompUnit, items);
};

}  // namespace ast