#pragma once

#include "frontend/ast/ast.h"
#include "frontend/ast/op.h"
#include "type.h"
#include "utils/error.h"

#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ir {

using SymDefNode = std::variant<const ast::ConstDef*, const ast::VarDef*, const ast::FuncParam*,
                                const ast::FuncDef*>;
using VarDefNode = std::variant<const ast::ConstDef*, const ast::VarDef*, const ast::FuncParam*>;
using FuncDefNode = const ast::FuncDef*;
using LValNode = const ast::LValExp*;
using ExprNode = std::variant<const ast::ConstDef*, const ast::VarDef*, const ast::FuncDef*,
                              const ast::LValExp*, const ast::Exp*, const ast::PrimaryExp*,
                              const ast::UnaryExp*, const ast::BinaryExp*, const ast::ExpBox*,
                              const ast::ConstExp*, const ast::TupleExp*, const ast::FuncParams*,
                              const ast::FuncParam*, const ast::ConstInitVal*, const ast::LVal*>;

using StmtNode =
    std::variant<const ast::StmtBox*, const ast::Stmt*, const ast::IfStmt*, const ast::WhileStmt*,
                 const ast::ReturnStmt*, const ast::AssignStmt*, const ast::BreakStmt*,
                 const ast::ContinueStmt*, const ast::BlockStmt*, const ast::ExpStmt*,
                 const ast::Decl*, const ast::ConstDecl*, const ast::VarDecl*>;

struct SemanticInfo {

    SemanticInfo(const ast::CompUnit& comp_unit) : tree(comp_unit) {
        analysis(&tree);
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

    auto definition_of(const LValNode& lval) {
        return defs[lval];
    }

    auto type_of(const ExprNode& expr) {
        return types[expr];
    }

private:
    const ast::CompUnit& tree;

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

    using VarTable = std::unordered_map<std::string, VarDefNode>;
    using FuncTable = std::unordered_map<std::string, FuncDefNode>;

    inline static const auto builtin_funcs = [] {
        std::vector<ast::FuncDef> v;
        v.emplace_back(ast::FuncDef{.type = ast::Type::INT, .name = "get_int"});
        v.emplace_back(ast::FuncDef{.type = ast::Type::FLOAT, .name = "get_float"});
        v.emplace_back(ast::FuncDef{.type = ast::Type::DOUBLE, .name = "get_double"});
        v.emplace_back(ast::FuncDef{
            .type = ast::Type::VOID, .name = "print_int", .params = {{.type = ast::Type::INT}}});
        v.emplace_back(ast::FuncDef{.type = ast::Type::VOID,
                                    .name = "print_float",
                                    .params = {{.type = ast::Type::FLOAT}}});
        v.emplace_back(ast::FuncDef{.type = ast::Type::VOID,
                                    .name = "print_double",
                                    .params = {{.type = ast::Type::DOUBLE}}});
        v.emplace_back(ast::FuncDef{
            .type = ast::Type::VOID, .name = "print_bool", .params = {{.type = ast::Type::BOOL}}});
        return v;
    }();

    inline const static auto VOID = adt::construct<const void>();
    inline const static auto NUM = adt::construct<std::variant<int, float, double>>();
    inline const static auto BOOL = adt::construct<bool>();
    inline const static auto INT = adt::construct<int>();

    inline const static auto ANY = adt::construct<std::any>();
    inline const static auto NEVER = adt::TypeBox{adt::Bottom{}.toBoxed()};

    std::vector<VarTable> vars;
    std::vector<FuncTable> funcs;
    void pushScope() {
        vars.emplace_back();
        funcs.emplace_back();
    }
    void popScope() {
        vars.pop_back();
        funcs.pop_back();
    }

    template <typename T>
    auto lookup(const std::string& name, const T& tables)
        -> std::optional<typename T::value_type::mapped_type> {
        for (auto it = tables.rbegin(); it != tables.rend(); it++) {
            auto found = it->find(name);
            if (found != it->end()) return found->second;
        }
        return std::nullopt;
    }

    adt::TypeBox calcType(ast::Type type, bool immutable = false) {
        switch (type) {
            case ast::Type::INT: return adt::Int{.immutable = immutable}.toBoxed();
            case ast::Type::FLOAT: return adt::Float{.immutable = immutable}.toBoxed();
            case ast::Type::BOOL: return adt::Bool{.immutable = immutable}.toBoxed();
            case ast::Type::DOUBLE: return adt::Double{.immutable = immutable}.toBoxed();
            case ast::Type::VOID: return adt::construct<const void>();
        }
    }

    adt::TypeBox calcType(const ast::FuncParam* param) {
        auto param_type = calcType(param->type);
        if (param->dims.size()) {
            for (size_t i = param->dims.size(); i > 1; i--) {
                param_type = adt::Slice(std::move(param_type), param->dims[i - 1]).toBoxed();
            }
            param_type = adt::Slice(std::move(param_type))
                             .toBoxed();  // NOTE: degrade sized array to unsized array for function
                                          // parameters
        }
        return param_type;
    }

    adt::Product calcType(const ast::TupleExp* tuple) {
        auto args_type = adt::Product{};
        for (const auto& arg : tuple->elements) {
            args_type.append(types[&arg]);
        }
        return args_type;
    }

    adt::Product calcType(const ast::FuncParams* params) {
        auto type = adt::Product{};
        for (auto& param : *params) {
            type.append(calcType(&param));
        }
        return type;
    }

    adt::Func calcType(const ast::FuncDef* func_def) {
        auto param_types = calcType(&func_def->params);
        auto ret_type = calcType(func_def->type);
        return {std::move(param_types), std::move(ret_type)};
    }

    template <typename T> void checkType(T node, Type upperbound) {
        if (!(types[node] <= upperbound)) {
            throw SemanticError(node->loc,
                                fmt::format("type error (at {}): `{}` is not subtype of `{}`",
                                            *node, types[node], upperbound));
        }
    }

    void registerSymbol(SymDefNode def) {
        match(
            def,
            [&](VarDefNode subdef) {
                match(subdef, [&](const auto& subdef) {
                    if (vars.back().count(subdef->name)) {
                        throw SemanticError(subdef->loc,
                                            fmt::format("redefinition of variable '{}' at depth {}",
                                                        subdef->name, vars.size() - 1));
                    }
                    vars.back()[subdef->name] = subdef;
                });
            },
            [&](FuncDefNode subdef) {
                if (funcs.back().count(subdef->name)) {
                    throw SemanticError(subdef->loc,
                                        fmt::format("redefinition of function '{}'", subdef->name));
                }
                funcs.back()[subdef->name] = subdef;
            });
    }

    template <typename T> void analysis(const T* elem) {}

    void analysis(const ast::CompUnit* comp_unit) {
        pushScope();  // builtin
        for (auto& func : builtin_funcs) {
            analysis(&func, true);
        }
        pushScope();  // global

        for (const auto& item : comp_unit->items) {
            match(item, [&](const auto& subitem) { analysis(&subitem); });
        }

        if (funcs.back().count("main") == 0) {
            throw SemanticError(comp_unit->loc, "function `main` is not defined");
        }
        auto main = funcs.back()["main"];
        checkType(main, adt::construct<int()>());
    }

    void analysis(const ast::Decl* decl) {
        bool is_const = std::holds_alternative<ast::ConstDecl>(*decl);
        match(*decl, [&](const auto& decl) {
            auto elem_type = calcType(decl.type, is_const);
            for (const auto& def : decl.defs) {
                registerSymbol(&def);
                if (def.dims.size()) {
                    // array type
                    auto elem_type = calcType(decl.type, is_const);
                    for (size_t i = def.dims.size(); i > 0; i--) {
                        elem_type = adt::Slice(std::move(elem_type), def.dims[i - 1]).toBoxed();
                    }
                    types[&def] = std::move(elem_type);
                } else {
                    types[&def] = elem_type;
                }
            }
        });
        match(*decl, [&](const auto& decl) { analysis(&decl); });
    }

    void analysis(const ast::ConstDecl* decl) {
        for (const auto& def : decl->defs) {
            Type val_type;
            analysis(&def.val);
            val_type = types[&def.val];
            if (!constructable(val_type, types[&def])) {
                throw SemanticError(def.loc,
                                    fmt::format("type error: cannot initialize `{}` with `{}`",
                                                types[&def], val_type));
            }
        }
    }

    void analysis(const ast::VarDecl* decl) {
        for (const auto& def : decl->defs) {
            Type val_type;
            if (def.val.has_value()) {
                analysis(&*def.val);
                val_type = types[&*def.val];
            } else {
                val_type = NEVER;
            }
            if (!constructable(val_type, types[&def])) {
                throw SemanticError(def.loc,
                                    fmt::format("type error: cannot initialize `{}` with `{}`",
                                                types[&def], val_type));
            }
        }
    }

    void analysis(const ast::ConstInitVal* val) {
        match(
            val->val,
            [&](const ast::ConstExp& exp) {
                analysis(&exp);
                types[val] = types[&exp];
            },
            [&](const std::vector<ast::ConstInitVal>& vals) {
                Type elem_type = NEVER;
                for (const auto& val : vals) {
                    analysis(&val);
                    elem_type = elem_type <= types[&val] ? types[&val] : elem_type;
                }
                types[val] = adt::Slice(std::move(elem_type), vals.size()).toBoxed();
            });
    }

    void analysis(const ast::FuncParam* param) {
        types[param] = calcType(param);
        registerSymbol(param);
    }

    void analysis(const ast::FuncParams* params) {
        types[params] = calcType(params).toBoxed();
        for (const auto& param : *params) {
            analysis(&param);
        }
    }

    void analysis(const ast::FuncDef* func_def, bool is_builtin = false) {
        types[func_def] = calcType(func_def).toBoxed();
        registerSymbol(func_def);
        pushScope();
        analysis(&func_def->params);
        analysis(&func_def->block);
        popScope();
        auto block_type = stmt_types[&func_def->block];
        if (!is_builtin && !block_type.always_return) {
            throw SemanticError(
                func_def->loc,
                fmt::format("function '{}' may not return on all paths", func_def->name));
        }
        auto ret_type = calcType(func_def).ret;
        if (!is_builtin && !(block_type.ret_type <= ret_type)) {
            throw SemanticError(
                func_def->loc,
                fmt::format("function '{}' has return type `{}`, but declared as `{}`",
                            func_def->name, block_type.ret_type, ret_type));
        }
    }

    void analysis(const ast::BlockStmt* block) {
        for (const auto& item : block->items) {
            match(
                item, [&](const ast::Decl& subitem) { analysis(&subitem); },
                [&](const ast::Stmt& subitem) {
                    analysis(&subitem);
                    stmt_types[block].append(stmt_types[&subitem]);
                });
        }
    }

    void analysis(const ast::StmtBox* stmt_box) {
        analysis(&*stmt_box->stmt);
        stmt_types[stmt_box] = stmt_types[&*stmt_box->stmt];
    }

    void analysis(const ast::Stmt* stmt) {
        match(
            *stmt,
            [&](const ast::BlockStmt& block) {
                pushScope(), analysis(&block), popScope();
                stmt_types[stmt] = stmt_types[&block];
            },
            [&](const auto& substmt) {
                analysis(&substmt);
                stmt_types[stmt] = stmt_types[&substmt];
            });
    }

    void analysis(const ast::IfStmt* if_stmt) {
        analysis(&if_stmt->cond, BOOL);
        analysis(&if_stmt->stmt);
        if (if_stmt->else_stmt) {
            auto else_stmt = &*if_stmt->else_stmt;
            analysis(else_stmt);
            stmt_types[if_stmt] = stmt_types[else_stmt];
        }
        stmt_types[if_stmt].merge(stmt_types[&if_stmt->stmt]);
    }

    void analysis(const ast::WhileStmt* while_stmt) {
        analysis(&while_stmt->cond, BOOL);
        analysis(&while_stmt->stmt);
        stmt_types[while_stmt].merge(stmt_types[&while_stmt->stmt]);
    }

    void analysis(const ast::ReturnStmt* return_stmt) {
        if (return_stmt->exp) {
            auto exp = &*return_stmt->exp;
            analysis(exp);
            stmt_types[return_stmt] = StmtType{.ret_type = types[exp], .always_return = true};
        } else {
            stmt_types[return_stmt] = StmtType{.ret_type = VOID, .always_return = true};
        }
    }

    void analysis(const ast::AssignStmt* assign_stmt) {
        auto var = &assign_stmt->var;
        auto exp = &assign_stmt->exp;
        analysis(var);
        analysis(exp, types[var]);
    }

    void analysis(const ast::ExpStmt* exp_stmt) {
        analysis(&exp_stmt->exp);
    }

    void analysis(const ast::LVal* lid, const Type& upperbound = ANY) {
        if (!upperbound.is<adt::Func>()) {
            auto symdef = lookup(lid->name, vars);
            if (!symdef) {
                throw SemanticError(lid->loc, fmt::format("undefined variable '{}'", lid->name));
            }
            match(*symdef, [&](const auto& def) { types[lid] = types[def]; });
        } else {
            auto funcdef = lookup(lid->name, funcs);
            if (!funcdef) {
                throw SemanticError(lid->loc, fmt::format("undefined function '{}'", lid->name));
            }
            types[lid] = types[*funcdef];
        }
        checkType(lid, upperbound);
    }

    void analysis(const ast::LValExp* lval, const Type& upperbound = ANY) {
        match(lval->val, [&](const auto& subexp) {
            analysis(&subexp, upperbound);
            types[lval] = types[&subexp];
        });
    }

    void analysis(const ast::Exp* exp, const Type& upperbound = ANY) {
        match(*exp, [&](const auto& subexp) {
            analysis(&subexp, upperbound);
            types[exp] = types[&subexp];
        });
    }

    void analysis(const ast::TupleExp* tuple, const Type& upperbound = ANY) {
        for (const auto& element : tuple->elements) {
            analysis(&element, ANY);
        }
        types[tuple] = calcType(tuple).toBoxed();
        checkType(tuple, upperbound);
    }

    void analysis(const ast::ConstExp* const_exp, const Type& upperbound = ANY) {
        types[const_exp] =
            match(*const_exp, [&](auto val) { return adt::construct<const decltype(val)>(); });
    }

    void analysis(const ast::PrimaryExp* primary, const Type& upperbound = ANY) {
        match(primary->exp, [&](const auto& subexp) {
            analysis(&subexp, upperbound);
            types[primary] = types[&subexp];
        });
        checkType(primary, upperbound);
    }

    void analysis(const ast::UnaryExp* unary_exp, const Type& upperbound = ANY) {
        auto exp = &unary_exp->exp;
        Type operand;
        switch (unary_exp->op) {
            case ast::UnaryOp::PLUS:
            case ast::UnaryOp::MINUS: operand = NUM; break;
            case ast::UnaryOp::NOT: operand = BOOL;
        }
        analysis(exp, operand);
        types[unary_exp] = types[exp];
        checkType(unary_exp, upperbound);
    }

    void analysis(const ast::BinaryExp* binary_exp, const Type& upperbound = ANY) {
        using namespace ast;
        auto left = &binary_exp->left;
        auto right = &binary_exp->right;
        Type lhs_bound, rhs_bound;
        switch (binary_exp->op) {
            case BinaryOp::AND:
            case BinaryOp::OR: rhs_bound = BOOL; break;
            case BinaryOp::INDEX: rhs_bound = INT; break;
            case BinaryOp::CALL: rhs_bound = ANY; break;
            default: rhs_bound = NUM; break;
        }
        analysis(right, rhs_bound);
        switch (binary_exp->op) {
            case BinaryOp::CALL:
                lhs_bound = adt::Func{types[right].as<adt::Product>(), upperbound}.toBoxed();
                break;
            case BinaryOp::INDEX: lhs_bound = adt::Slice{upperbound, std::nullopt}.toBoxed(); break;
            case BinaryOp::AND:
            case BinaryOp::OR: lhs_bound = BOOL; break;
            default: rhs_bound = NUM; break;
        }
        analysis(left, lhs_bound);
        if (binary_exp->op != BinaryOp::INDEX && binary_exp->op != BinaryOp::CALL) {
            if (!(types[left] <= types[right]) && !(types[right] <= types[left])) {
                throw SemanticError(
                    binary_exp->loc,
                    fmt::format("type error: cannot apply operator `{}` to `{}` and `{}`",
                                binary_exp->op, types[left], types[right]));
            }
        }
        switch (binary_exp->op) {
            case BinaryOp::EQ:
            case BinaryOp::NEQ:
            case BinaryOp::LT:
            case BinaryOp::GT:
            case BinaryOp::LEQ:
            case BinaryOp::GEQ: types[binary_exp] = BOOL; break;
            case BinaryOp::INDEX: types[binary_exp] = types[left].as<adt::Slice>().elem; break;
            case BinaryOp::CALL: types[binary_exp] = types[left].as<adt::Func>().ret; break;
            default:
                types[binary_exp] = types[left] <= types[right] ? types[right] : types[left];
                break;
        }
        checkType(binary_exp, upperbound);
    }

    void analysis(const ast::ExpBox* exp_box, const Type& upperbound = ANY) {
        auto exp = exp_box->exp.get();
        analysis(exp, upperbound);
        types[exp_box] = types[exp];
        checkType(exp_box, upperbound);
    }
};

}  // namespace ir
