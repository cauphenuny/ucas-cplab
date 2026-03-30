#pragma once

#include "op.h"
#include "semantic.h"
#include "type.h"

#include <variant>

namespace ir {

using Type = adt::TypeBox;

struct Block;

struct NamedValue {
    Type type;
    SymDefNode def;

    SIMPLE_TO_STRING(match(
        def, [&](const ast::FuncDef* f) { return f->name; },
        [&](auto v) { return fmt::format("{}_{}_{}", v->name, v->loc.line, v->loc.col); }));
};

struct TempValue {
    Type type;
    size_t id;
    const Block* scope;

    SIMPLE_TO_STRING(fmt::format("${}", id));
};

struct ConstexprValue {
    Type type;
    std::variant<int, float, bool, double, std::monostate> val;

    SIMPLE_TO_STRING(match(
                         val, [&](std::monostate) { return std::string(""); },
                         [&](auto v) { return fmt::format("{}", v); }););
};

using LeftValue = std::variant<NamedValue, TempValue>;
using Value = std::variant<NamedValue, TempValue, ConstexprValue>;

struct AggregateInst {
    LeftValue result;
    std::vector<Value> src;

    [[nodiscard]] auto toString() const {
        std::string rhs;
        for (auto&& item : src) {
            rhs += fmt::format("{}, ", item);
        }
        rhs.pop_back();
        if (rhs.size() > 1) rhs.pop_back();
        return fmt::format("let {} = ({})", result, rhs);
    }
};

struct RegularInst {
    InstOp op;
    LeftValue result;
    Value lhs, rhs;

    SIMPLE_TO_STRING(op == InstOp::LOAD    ? fmt::format("let {} = {}[{}]", result, lhs, rhs)
                     : op == InstOp::STORE ? fmt::format("let {}[{}] = {}", result, lhs, rhs)
                     : op == InstOp::CALL  ? fmt::format("let {} = {}({})", result, lhs, rhs)
                                           : fmt::format("let {} = {} {} {}", result, lhs, op, rhs))
};

using Inst = std::variant<RegularInst, AggregateInst>;

struct ReturnExit {
    Value exp;

    SIMPLE_TO_STRING(fmt::format("return {}", exp));
};

struct BranchExit {
    Value cond;
    const Block *true_target, *false_target;
    [[nodiscard]] auto toString() const -> std::string;
};

struct JumpExit {
    const Block* target;
    [[nodiscard]] auto toString() const -> std::string;
};

using Exit = std::variant<BranchExit, JumpExit, ReturnExit>;

struct Block {
    const std::string label;
    std::vector<Inst> insts;
    Exit exit;

    [[nodiscard]] auto toString() const {
        std::string str;
        str += fmt::format("{}:\n", label);
        for (const auto& inst : insts) {
            str += fmt::format("  {}\n", inst);
        }
        str += fmt::format("  {}\n", exit);
        return str;
    }
};

auto BranchExit::toString() const -> std::string {
    return fmt::format("br {}? {} : {}", cond, true_target ? true_target->label : "<unknown>",
                       false_target ? false_target->label : "<unknown>");
}

auto JumpExit::toString() const -> std::string {
    return fmt::format("jump {}", target ? target->label : "<unknown>");
}

struct Alloc {
    NamedValue var;
    SIMPLE_TO_STRING(fmt::format("var {}: {}", var, var.type));
};

struct Func {
    const Type type;
    const std::string name;
    std::vector<Alloc> locals;
    std::vector<Block> blocks;

    [[nodiscard]] auto toString() const {
        std::string str;
        str += fmt::format("func {}{}:\n", name, type);
        for (const auto& def : locals) {
            str += fmt::format("  {}\n", def);
        }
        for (const auto& block : blocks) {
            str += fmt::format("{}", block);
        }
        return str;
    }
};

struct Program {
    std::vector<Alloc> globals;
    std::vector<Func> funcs;
    Func* entrance;

    [[nodiscard]] auto toString() const {
        std::string str;
        for (const auto& def : globals) {
            str += fmt::format("{}\n", def);
        }
        str += "\n";
        for (const auto& func : funcs) {
            str += fmt::format("{}\n", func);
        }
        return str;
    }

    Program(const SemanticAST& ast) : ast(ast) {}
    const SemanticAST& ast;
};

}  // namespace ir