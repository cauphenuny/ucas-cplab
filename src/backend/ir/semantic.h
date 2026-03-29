#pragma once

#include "adt.h"
#include "frontend/ast/ast.h"
#include "utils/error.h"

#include <string>
#include <variant>
#include <vector>

namespace ir {

struct SemanticAST {
    using Type = adt::TypeBox;
    const ast::CompUnit tree;

    SemanticAST(ast::CompUnit comp_unit) : tree(std::move(comp_unit)) {
        analysis(&tree);
    }

    void analysis(const ast::CompUnit* comp_unit) {
        vars.emplace_back(), funcs.emplace_back();  // builtin
        for (auto& func : builtin_funcs) {
            funcs.back()[func.name] = &func;
        }
        vars.emplace_back(), funcs.emplace_back();  // global

        for (const auto& item : comp_unit->items) {
            match(item, [&](const auto& subitem) { analysis(&subitem); });
        }

        if (funcs.back().count("main") == 0) {
            throw SemanticError(comp_unit->loc, "function `main` is not defined");
        }
        auto main = funcs.back()["main"];
        auto main_type = convert(main);
        if (!(main_type.ret <= adt::construct<int>())) {
            throw SemanticError(
                main->loc,
                fmt::format("function `main` should return int, but got `{}`", main_type.ret));
        }
        if (!(main_type.params <= adt::construct<void>())) {
            throw SemanticError(
                main->loc, fmt::format("function `main` should not have parameters, but got `{}`",
                                       main_type.params));
        }
    }

    void show() const {
        fmt::println("Types: ");
        for (const auto& pair : type) {
            match(pair.first,
                  [&](auto subelem) { fmt::println("typeof({}) = {}", *subelem, pair.second); });
        }
        fmt::println("\nDefinitions: ");
        for (const auto& pair : def) {
            match(pair.second,
                  [&](auto subdef) { fmt::println("def({}) = {}", *pair.first, *subdef); });
        }
    }

private:
    using ASTNode = std::variant<
        const ast::CompUnit*, const ast::Decl*, const ast::ConstDecl*, const ast::ConstDef*,
        const ast::VarDecl*, const ast::VarDef*, const ast::FuncDef*, const ast::BlockStmt*,
        const ast::StmtBox*, const ast::Stmt*, const ast::IfStmt*, const ast::WhileStmt*,
        const ast::ReturnStmt*, const ast::AssignStmt*, const ast::BreakStmt*,
        const ast::ContinueStmt*, const ast::LValExp*, const ast::Exp*, const ast::PrimaryExp*,
        const ast::UnaryExp*, const ast::BinaryExp*, const ast::ExpBox*, const ast::ConstExp*,
        const ast::CallExp*, const ast::FuncParams*, const ast::FuncParam*, const ast::FuncArgs*,
        const ast::ConstInitVal*>;

    using SymDefNode = std::variant<const ast::ConstDef*, const ast::VarDef*, const ast::FuncParam*,
                                    const ast::FuncDef*>;
    using LValNode = const ast::LValExp*;

    std::unordered_map<ASTNode, Type> type;
    std::unordered_map<LValNode, SymDefNode> def;

    using VarDefNode =
        std::variant<const ast::ConstDef*, const ast::VarDef*, const ast::FuncParam*>;
    using FuncDefNode = const ast::FuncDef*;
    using VarTable = std::unordered_map<std::string, VarDefNode>;
    using FuncTable = std::unordered_map<std::string, FuncDefNode>;

    inline static const auto builtin_funcs = [] {
        std::vector<ast::FuncDef> v;
        v.emplace_back(ast::FuncDef{.type = ast::Type::INT, .name = "get_int"});
        v.emplace_back(ast::FuncDef{.type = ast::Type::FLOAT, .name = "get_float"});
        v.emplace_back(ast::FuncDef{.type = ast::Type::DOUBLE, .name = "get_double"});
        v.emplace_back(ast::FuncDef{
            .type = ast::Type::VOID, .name = "put_int", .params = {{.type = ast::Type::INT}}});
        v.emplace_back(ast::FuncDef{.type = ast::Type::VOID,
                                     .name = "put_float",
                                     .params = {{.type = ast::Type::FLOAT}}});
        v.emplace_back(ast::FuncDef{.type = ast::Type::VOID,
                                     .name = "put_double",
                                     .params = {{.type = ast::Type::DOUBLE}}});
        return v;
    }();

    inline const static auto VOID = adt::construct<void>();
    inline const static auto NUM = adt::construct<std::variant<int, float, double>>();
    inline const static auto BOOL = adt::construct<bool>();
    inline const static auto ANY = adt::construct<std::any>();
    inline const static auto NEVER = adt::TypeBox{adt::Bottom{}.toBoxed()};

    std::vector<VarTable> vars;
    std::vector<FuncTable> funcs;
    void push() {
        vars.emplace_back();
        funcs.emplace_back();
    }
    void pop() {
        vars.pop_back();
        funcs.pop_back();
    }

    adt::TypeBox convert(ast::Type type) {
        switch (type) {
            case ast::Type::INT: return adt::Int{}.toBoxed();
            case ast::Type::FLOAT: return adt::Float{}.toBoxed();
            case ast::Type::BOOL: return adt::Bool{}.toBoxed();
            case ast::Type::VOID: return adt::Product{}.toBoxed();
            case ast::Type::DOUBLE: return adt::Double{}.toBoxed();
        }
    }

    adt::Product convert(const ast::FuncParams* params) {
        auto type = adt::Product{};
        for (auto& param : *params) {
            type.append(convert(param.type));
        }
        return type;
    }

    adt::Func convert(const ast::FuncDef* func_def) {
        auto param_types = convert(&func_def->params);
        auto ret_type = convert(func_def->type);
        return {std::move(param_types), std::move(ret_type)};
    }

    template <typename T> void check(T node, Type upperbound) {
        if (!(type[node] <= upperbound)) {
            throw SemanticError(node->loc, fmt::format("type error: `{}` is not subtype of `{}`",
                                                       type[node], upperbound));
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

    template <typename T> void analysis(const T* elem) {
        type[elem];
    }

    void analysis(const ast::Decl* decl) {
        match(*decl, [&](const auto& decl) {
            type[&decl] = adt::construct<void>();
            auto elem_type = convert(decl.type);
            for (const auto& def : decl.defs) {
                registerSymbol(&def);
                if (def.dims.size()) {
                    // array type
                    auto elem_type = convert(decl.type);
                    for (size_t i = def.dims.size(); i > 0; i--) {
                        elem_type = adt::Slice(std::move(elem_type), def.dims[i - 1]).toBoxed();
                    }
                    type[&def] = std::move(elem_type);
                } else {
                    type[&def] = elem_type;
                }
            }
        });
        match(*decl, [&](const auto& decl) { analysis(&decl); });
    }

    void analysis(const ast::ConstDecl* decl) {
        for (const auto& def : decl->defs) {
            Type val_type;
            analysis(&def.val);
            val_type = type[&def.val];
            if (!constructable(val_type, type[&def])) {
                throw SemanticError(def.loc,
                                    fmt::format("type error: cannot initialize `{}` with `{}`",
                                                type[&def], val_type));
            }
        }
    }

    void analysis(const ast::VarDecl* decl) {
        for (const auto& def : decl->defs) {
            Type val_type;
            if (def.val.has_value()) {
                analysis(&*def.val);
                val_type = type[&*def.val];
            } else {
                val_type = NEVER;
            }
            if (!constructable(val_type, type[&def])) {
                throw SemanticError(def.loc,
                                    fmt::format("type error: cannot initialize `{}` with `{}`",
                                                type[&def], val_type));
            }
        }
    }

    void analysis(const ast::ConstInitVal* val) {
        match(
            val->val,
            [&](const ast::ConstExp& exp) {
                analysis(&exp);
                type[val] = type[&exp];
            },
            [&](const std::vector<ast::ConstInitVal>& vals) {
                Type elem_type = NEVER;
                for (const auto& val : vals) {
                    analysis(&val);
                    elem_type = elem_type <= type[&val] ? type[&val] : elem_type;
                }
                type[val] = adt::Slice(std::move(elem_type), vals.size()).toBoxed();
            });
    }

    void analysis(const ast::FuncParam* param) {
        auto param_type = convert(param->type);
        if (param->dims.size()) {
            auto param_type = convert(param->type);
            for (size_t i = param->dims.size(); i > 0; i--) {
                param_type = adt::Slice(std::move(param_type), param->dims[i - 1]).toBoxed();
            }
        }
        type[param] = std::move(param_type);
        registerSymbol(param);
    }

    void analysis(const ast::FuncParams* params) {
        type[params] = convert(params).toBoxed();
        for (const auto& param : *params) {
            analysis(&param);
        }
    }

    void analysis(const ast::FuncArgs* args) {
        for (const auto& arg : *args) {
            analysis(&arg);
        }
        auto args_type = adt::Product{};
        for (const auto& arg : *args) {
            args_type.append(type[&arg]);
        }
        type[args] = std::move(args_type).toBoxed();
    }

    void analysis(const ast::FuncDef* func_def) {
        type[func_def] = convert(func_def).toBoxed();
        registerSymbol(func_def);
        push();
        analysis(&func_def->params);
        analysis(&func_def->block);
        pop();
        auto block_type = type[&func_def->block];
        auto ret_type = convert(func_def).ret;
        if (!(block_type <= ret_type)) {
            throw SemanticError(
                func_def->loc,
                fmt::format("function '{}' has return type `{}`, but declared as `{}`",
                            func_def->name, block_type, ret_type));
        }
    }

    void analysis(const ast::BlockStmt* block) {
        bool returned = false;
        for (const auto& item : block->items) {
            match(
                item, [&](const ast::Decl& subitem) { analysis(&subitem); },
                [&](const ast::Stmt& subitem) {
                    analysis(&subitem);
                    if (std::holds_alternative<ast::ReturnStmt>(subitem)) {
                        returned = true;
                        type[block] = type[&subitem];
                    }
                });
            if (returned) break;
        }
        if (!returned) type[block] = adt::construct<void>();
    }

    void analysis(const ast::StmtBox* stmt_box) {
        analysis(&*stmt_box->stmt);
        type[stmt_box] = type[&*stmt_box->stmt];
    }

    void analysis(const ast::Stmt* stmt) {
        match(
            *stmt,
            [&](const ast::BlockStmt& block) {
                push(), analysis(&block), pop();
                type[stmt] = type[&block];
            },
            [&](const auto& substmt) {
                analysis(&substmt);
                type[stmt] = type[&substmt];
            });
    }

    void analysis(const ast::IfStmt* if_stmt) {
        analysis(&if_stmt->cond, BOOL);
        analysis(&if_stmt->stmt);
        if (if_stmt->else_stmt) {
            analysis(&*if_stmt->else_stmt);
        }
    }

    void analysis(const ast::WhileStmt* while_stmt) {
        analysis(&while_stmt->cond, BOOL);
        analysis(&while_stmt->stmt);
    }

    void analysis(const ast::ReturnStmt* return_stmt) {
        if (return_stmt->exp) {
            auto exp = &*return_stmt->exp;
            analysis(exp);
            type[return_stmt] = type[exp];
        } else {
            type[return_stmt] = adt::construct<void>();
        }
    }

    void analysis(const ast::AssignStmt* assign_stmt) {
        type[assign_stmt] = adt::construct<void>();
        auto var = &assign_stmt->var;
        auto exp = &assign_stmt->exp;
        analysis(var);
        analysis(exp);
        if (!(type[exp] <= type[var])) {
            throw SemanticError(assign_stmt->loc,
                                fmt::format("cannot assign `{}` to `{}`", type[exp], type[var]));
        }
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

    void analysis(const ast::LValExp* lval, const Type& upperbound = ANY, bool isfunc = false) {
        if (isfunc) {
            auto symdef = lookup(lval->name, funcs);
            if (!symdef) {
                throw SemanticError(lval->loc, fmt::format("undefined function '{}'", lval->name));
            }
            type[lval] = type[*symdef];
        } else {
            auto symdef = lookup(lval->name, vars);
            if (!symdef) {
                throw SemanticError(lval->loc, fmt::format("undefined variable '{}'", lval->name));
            }
            match(*symdef, [&](const auto& def) {
                if (lval->indices.size() > def->dims.size()) {
                    throw SemanticError(
                        lval->loc, fmt::format("too many indices for variable '{}'", lval->name));
                }
                auto elem_type = type[def];
                for (auto& index : lval->indices) {
                    if (!index) {
                        throw SemanticError(
                            lval->loc, fmt::format("missing index in l-value '{}'", lval->name));
                    }
                    analysis(&*index, adt::construct<int>());
                    match(
                        elem_type.var(), [&](const adt::Slice& slice) { elem_type = slice.elem; },
                        [&](const auto&) {
                            throw SemanticError(
                                lval->loc,
                                fmt::format("variable '{}' is not subscriptable", lval->name));
                        });
                }
                type[lval] = elem_type;
            });
        }
        check(lval, upperbound);
    }

    void analysis(const ast::Exp* exp, const Type& upperbound = ANY) {
        match(*exp, [&](const auto& subexp) {
            analysis(&subexp, upperbound);
            type[exp] = type[&subexp];
        });
    }

    void analysis(const ast::ConstExp* const_exp, const Type& upperbound = ANY) {
        type[const_exp] =
            match(*const_exp, [&](auto val) { return adt::construct<decltype(val)>(); });
    }

    void analysis(const ast::PrimaryExp* primary, const Type& upperbound = ANY,
                  bool isfunc = false) {
        match(
            primary->exp,
            [&](const ast::LValExp& lval) {
                analysis(&lval, upperbound, isfunc);
                type[primary] = type[&lval];
            },
            [&](const auto& subexp) {
                analysis(&subexp, upperbound);
                type[primary] = type[&subexp];
            });
        check(primary, upperbound);
    }

    void analysis(const ast::CallExp* call_exp, const Type& upperbound = ANY) {
        auto func = &call_exp->func;
        auto args = &call_exp->args;
        analysis(func, upperbound, true);
        analysis(args);
        auto func_def = *lookup(func->name, funcs);
        auto func_type = convert(func_def);
        if (!(type[args] <= func_type.params)) {
            throw SemanticError(call_exp->loc,
                                fmt::format("function '{}': `{}` can not be called with `{}`",
                                            func->name, func_type, type[args], func_type.params));
        }
        type[call_exp] = func_type.ret;
        check(call_exp, upperbound);
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
        type[unary_exp] = type[exp];
        check(unary_exp, upperbound);
    }

    void analysis(const ast::BinaryExp* binary_exp, const Type& upperbound = ANY) {
        auto left = &binary_exp->left;
        auto right = &binary_exp->right;
        Type operand;
        std::optional<Type> result;
        switch (binary_exp->op) {
            case ast::BinaryOp::ADD:
            case ast::BinaryOp::SUB:
            case ast::BinaryOp::MUL:
            case ast::BinaryOp::DIV:
            case ast::BinaryOp::MOD: operand = NUM; break;
            case ast::BinaryOp::EQ:
            case ast::BinaryOp::NEQ:
            case ast::BinaryOp::LT:
            case ast::BinaryOp::GT:
            case ast::BinaryOp::LEQ:
            case ast::BinaryOp::GEQ:
                operand = NUM;
                result = BOOL;
                break;
            case ast::BinaryOp::AND:
            case ast::BinaryOp::OR:
                operand = BOOL;
                result = BOOL;
                break;
        }
        analysis(left, operand);
        analysis(right, operand);
        if ((!(type[left] <= type[right])) && !(type[right] <= type[left])) {
            throw SemanticError(binary_exp->loc, fmt::format("type mismatch between `{}` and `{}`",
                                                             type[left], type[right]));
        }
        if (result)
            type[binary_exp] = *result;
        else {
            type[binary_exp] = type[left] <= type[right] ? type[right] : type[left];
        }
        check(binary_exp, upperbound);
    }

    void analysis(const ast::ExpBox* exp_box, const Type& upperbound = ANY) {
        auto exp = exp_box->exp.get();
        analysis(exp, upperbound);
        type[exp_box] = type[exp];
        check(exp_box, upperbound);
    }
};

}  // namespace ir
