#pragma once

#include "backend/ir/type.hpp"
#include "frontend/ast/ast.hpp"
#include "frontend/ast/op.hpp"
#include "utils/error.hpp"

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ast {

using SymDefNode = std::variant<const ConstDef*, const VarDef*, const FuncParam*, const FuncDef*>;
using VarDefNode = std::variant<const ConstDef*, const VarDef*, const FuncParam*>;
using FuncDefNode = const FuncDef*;
using LValNode = const LVal*;
using ExprNode = std::variant<const ConstDef*, const VarDef*, const FuncDef*, const LValExp*,
                              const Exp*, const PrimaryExp*, const UnaryExp*, const BinaryExp*,
                              const ExpBox*, const ConstExp*, const CallExp*, const FuncParams*,
                              const FuncArgs*, const FuncParam*, const ConstInitVal*, const LVal*>;

using StmtNode =
    std::variant<const StmtBox*, const Stmt*, const IfStmt*, const WhileStmt*, const ReturnStmt*,
                 const AssignStmt*, const BreakStmt*, const ContinueStmt*, const BlockStmt*,
                 const ExpStmt*, const Decl*, const ConstDecl*, const VarDecl*>;

struct SemanticAST {

    SemanticAST(std::unique_ptr<CompUnit> comp_unit) : tree(std::move(comp_unit)) {
        analysis(tree.get());
    }

    void show() const {
        fmt::println("===== Definitions =====\n");
        for (const auto& pair : defs) {
            match(pair.second,
                  [&](auto subdef) { fmt::println("def({}) = {}", *pair.first, *subdef); });
        }
        fmt::println("\n===== Types =====\n");
        for (const auto& pair : types) {
            match(pair.first,
                  [&](auto subelem) { fmt::println("type({}) = {}", *subelem, pair.second); });
        }
        fmt::println("\n===== Statement types =====\n");
        for (const auto& pair : stmt_types) {
            match(pair.first,
                  [&](auto substmt) { fmt::println("type({}) = {}", *substmt, pair.second); });
        }
    }

    [[nodiscard]] auto definition_of(const LValNode& lval) const {
        try {
            return defs.at(lval);
        } catch (const std::out_of_range&) {
            throw COMPILER_ERROR(fmt::format("can not find definition of variable '{}'", *lval));
        }
    }

    [[nodiscard]] auto type_of(const ExprNode& expr) const {
        try {
            return types.at(expr);
        } catch (const std::out_of_range&) {
            return match(expr, [&](auto subexpr) -> Type {
                throw COMPILER_ERROR(fmt::format("can not find type of expression '{}'", *subexpr));
            });
        }
    }

    [[nodiscard]] auto type_of(const StmtNode& stmt) const {
        try {
            return stmt_types.at(stmt);
        } catch (const std::out_of_range&) {
            return match(stmt, [&](auto substmt) -> StmtType {
                throw COMPILER_ERROR(fmt::format("can not find type of statement '{}'", *substmt));
            });
        }
    }

    [[nodiscard]] auto& ast() const {
        return *tree;
    }

    using VarTable = std::unordered_map<std::string, VarDefNode>;
    using FuncTable = std::unordered_map<std::string, std::pair<FuncDefNode, bool>>;

    FuncTable funcs;

private:
    const std::unique_ptr<const CompUnit> tree;

    using Type = adt::TypeBox;

    std::unordered_map<LValNode, SymDefNode> defs;

    std::unordered_map<ExprNode, Type> types;

    struct StmtType {
        Type ret_type{NEVER};
        bool always_return{false};
        void append(const StmtType& next) {
            if (always_return) return;  // if already always return, no need to append
            ret_type = ret_type | next.ret_type;
            always_return = next.always_return;
        }
        void merge(const StmtType& next) {
            ret_type = ret_type | next.ret_type;
            always_return = always_return && next.always_return;
        }
        TO_STRING(StmtType, ret_type, always_return);
    };
    std::unordered_map<StmtNode, StmtType> stmt_types;

    inline const static auto VOID = adt::construct<const void>();
    inline const static auto NUM = adt::construct<std::variant<int, float, double>>();
    inline const static auto BOOL = adt::construct<bool>();
    inline const static auto INT = adt::construct<int>();

    inline const static auto ANY = adt::construct<std::any>();
    inline const static auto NEVER = adt::TypeBox{adt::Bottom{}.toBoxed()};

    template <typename T> void analysis(const T* elem) {}

    void analysis(const CompUnit* comp_unit);
    void analysis(const Decl* decl);
    void analysis(const ConstDecl* decl);
    void analysis(const VarDecl* decl);
    void analysis(const ConstInitVal* val);

    void analysis(const FuncParam* param);
    void analysis(const FuncParams* params);
    void analysis(const FuncArgs* args);
    void analysis(const FuncDef* func_def, bool is_builtin = false);

    void analysis(const Stmt* stmt);
    void analysis(const BlockStmt* block);
    void analysis(const IfStmt* if_stmt);
    void analysis(const WhileStmt* while_stmt);
    void analysis(const ReturnStmt* return_stmt);
    void analysis(const AssignStmt* assign_stmt);
    void analysis(const ExpStmt* exp_stmt);
    void analysis(const StmtBox* stmt_box);

    void analysis(const Exp* exp, const Type& upperbound = ANY);
    void analysis(const LVal* lid, const Type& upperbound = ANY);
    void analysis(const LValExp* lval, const Type& upperbound = ANY);
    void analysis(const CallExp* call, const Type& upperbound = ANY);
    void analysis(const ConstExp* const_exp, const Type& upperbound = ANY);
    void analysis(const PrimaryExp* primary, const Type& upperbound = ANY);
    void analysis(const UnaryExp* unary_exp, const Type& upperbound = ANY);
    void analysis(const BinaryExp* binary_exp, const Type& upperbound = ANY);
    void analysis(const ExpBox* exp_box, const Type& upperbound = ANY);

    auto calcType(ast::Type type, bool immutable = false) -> adt::TypeBox;
    auto calcType(const FuncParam* param) -> adt::TypeBox;
    auto calcType(const FuncArgs* args) -> adt::Product;
    auto calcType(const FuncParams* params) -> adt::Product;
    auto calcType(const FuncDef* func_def) -> adt::Func;

    template <typename T> void checkType(T node, Type upperbound) {
        if (!(types[node] <= upperbound)) {
            throw SemanticError(node->loc,
                                fmt::format("type error (at {}): `{}` is not subtype of `{}`",
                                            *node, types[node], upperbound));
        }
    }

    inline static const auto builtin_funcs = [] {
        std::vector<FuncDef> v;
        v.emplace_back(FuncDef{.type = ast::Type::INT, .name = "get_int"});
        v.emplace_back(FuncDef{.type = ast::Type::FLOAT, .name = "get_float"});
        v.emplace_back(FuncDef{.type = ast::Type::DOUBLE, .name = "get_double"});
        v.emplace_back(FuncDef{
            .type = ast::Type::VOID, .name = "print_int", .params = {{.type = ast::Type::INT}}});
        v.emplace_back(FuncDef{.type = ast::Type::VOID,
                               .name = "print_float",
                               .params = {{.type = ast::Type::FLOAT}}});
        v.emplace_back(FuncDef{.type = ast::Type::VOID,
                               .name = "print_double",
                               .params = {{.type = ast::Type::DOUBLE}}});
        v.emplace_back(FuncDef{
            .type = ast::Type::VOID, .name = "print_bool", .params = {{.type = ast::Type::BOOL}}});
        return v;
    }();

    std::vector<VarTable> vars;
    void pushScope();
    void popScope();

    auto lookup(const std::string& name) -> std::optional<VarDefNode> {
        for (auto it = vars.rbegin(); it != vars.rend(); it++) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        return std::nullopt;
    }

    void registerVariable(VarDefNode def);
    void registerFunction(FuncDefNode def, bool is_builtin = false);
};

inline auto analysis(std::unique_ptr<CompUnit> comp_unit) -> SemanticAST {
    return {std::move(comp_unit)};
}

}  // namespace ast
