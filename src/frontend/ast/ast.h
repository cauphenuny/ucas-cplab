#pragma once

#include <cstdint>
#include <string>
#include <utils/serialize.hpp>
#include <utils/traits.hpp>
#include <vector>

namespace ast {

enum class Type : uint8_t {
    INT,
    FLOAT,
    BOOL,
    DOUBLE,
    VOID,
};

auto toString(Type type) -> std::string {
    switch (type) {
        case Type::INT: return "int";
        case Type::FLOAT: return "float";
        case Type::BOOL: return "bool";
        case Type::DOUBLE: return "double";
        case Type::VOID: return "void";
    }
}

struct CompUnit;
struct ConstDecl;
struct VarDecl;
struct ConstDef;
struct VarDef;

using Decl = std::variant<ConstDecl, VarDecl>;

struct FuncDef;
struct FuncParam;
using FuncParams = std::vector<FuncParam>;
struct FuncArgs;

struct Block;

struct IfStmt;
struct WhileStmt;
struct ReturnStmt;
struct BreakStmt;
struct ContinueStmt;
struct AssignStmt;

struct LeftVal;
struct PrimaryExp;
struct UnaryExp;
struct CallExp;
struct BinaryExp;

struct Exp;
struct Stmt;
using ExpBox = std::unique_ptr<Exp>;
using StmtBox = std::unique_ptr<Stmt>;

using BlockItem = std::variant<Decl, Stmt>;

using ConstExp = std::variant<int, float, double, bool>;

struct ConstInitVal;

struct ConstInitVal {
    std::variant<ConstExp, std::vector<std::unique_ptr<ConstInitVal>>> val;
    TO_STRING(ConstInitVal, val);
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

struct LeftVal {
    std::string name;
    std::vector<Exp> indices;
    TO_STRING(LeftVal, name, indices);
};

struct PrimaryExp : traits::ToBoxed<PrimaryExp, Exp> {
private:
    using T = std::variant<ExpBox, LeftVal, ConstExp>;
    T exp;

public:
    PrimaryExp(T exp) : exp(std::move(exp)) {}
    TO_STRING(PrimaryExp, exp);
};

enum class UnaryOp : uint8_t { PLUS, MINUS, NOT };

std::string toString(UnaryOp op) {
    switch (op) {
        case UnaryOp::PLUS: return "+";
        case UnaryOp::MINUS: return "-";
        case UnaryOp::NOT: return "!";
        default: throw std::runtime_error(fmt::format("invalid unary op: {}", (uint8_t)op));
    }
}

struct UnaryExp : traits::ToBoxed<UnaryExp, Exp> {
    UnaryOp op;
    ExpBox exp;
    TO_STRING(UnaryExp, op, exp);
};

struct FuncArgs {
    std::vector<Exp> args;
    TO_STRING(FuncArgs, args);
};

struct CallExp {
    std::string name;
    FuncArgs args;
    TO_STRING(CallExp, name, args);
};

enum class BinaryOp : uint8_t {
    MUL,
    DIV,
    MOD,  //
    ADD,
    SUB,  //
    LT,
    GT,
    LEQ,
    GEQ,  //
    EQ,
    NEQ,  //
    AND,
    OR,
};

std::string toString(BinaryOp op) {
    switch (op) {
        case BinaryOp::MUL: return "*";
        case BinaryOp::DIV: return "/";
        case BinaryOp::MOD: return "%";
        case BinaryOp::ADD: return "+";
        case BinaryOp::SUB: return "-";
        case BinaryOp::LT: return "<";
        case BinaryOp::GT: return ">";
        case BinaryOp::LEQ: return "<=";
        case BinaryOp::GEQ: return ">=";
        case BinaryOp::EQ: return "==";
        case BinaryOp::NEQ: return "!=";
        case BinaryOp::AND: return "&&";
        case BinaryOp::OR: return "||";
        default: throw std::runtime_error(fmt::format("invalid binary op: {}", (uint8_t)op));
    }
}

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
    [[nodiscard]] auto toString() const {
        return serialize(exp);
    }
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
    LeftVal var;
    Exp exp;
    TO_STRING(AssignStmt, var, exp);
};

struct Stmt : traits::ToBoxed<Stmt> {
private:
    using T = std::variant<IfStmt, WhileStmt, ReturnStmt, BreakStmt, ContinueStmt, AssignStmt>;
    T stmt;

public:
    Stmt(T stmt) : stmt(std::move(stmt)) {}
    [[nodiscard]] auto toString() const {
        return serialize(stmt);
    }
};

struct Block {
    std::vector<BlockItem> items;
    TO_STRING(Block, items);
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
    TO_STRING(CompUnit, items);
};

}  // namespace ast