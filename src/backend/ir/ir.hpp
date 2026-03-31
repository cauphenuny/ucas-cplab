#pragma once

#include "frontend/ast/analysis/semantic_ast.h"
#include "op.hpp"
#include "type.hpp"

#include <utility>
#include <variant>

namespace ir {

using Type = adt::TypeBox;

struct Block;
struct Func;
namespace gen {
struct Generator;
}

struct NamedValue {
    Type type;
    ast::SymDefNode def;

    SIMPLE_TO_STRING(match(
        def, [&](const ast::FuncDef* f) { return f->name; },
        [&](auto v) { return fmt::format("{}_{}", v->name, v->loc); }));
};

struct TempValue {
    Type type;
    size_t id;

    SIMPLE_TO_STRING(fmt::format("${}", id));
};

struct ConstexprValue {
    Type type;
    std::variant<std::monostate, int, float, bool, double> val;

    SIMPLE_TO_STRING(match(
                         val, [&](std::monostate) { return std::string(""); },
                         [&](auto v) { return fmt::format("{}", v); }););

    ConstexprValue() : type(adt::construct<void>()), val(std::monostate{}) {}
    ConstexprValue(int v) : type(adt::construct<int>()), val(v) {}
    ConstexprValue(float v) : type(adt::construct<float>()), val(v) {}
    ConstexprValue(double v) : type(adt::construct<double>()), val(v) {}
    ConstexprValue(bool v) : type(adt::construct<bool>()), val(v) {}
};

using LeftValue = std::variant<NamedValue, TempValue>;
using Value = std::variant<ConstexprValue, NamedValue, TempValue>;

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

    SIMPLE_TO_STRING(op == InstOp::MOV     ? fmt::format("let {} = {}", result, lhs)
                     : op == InstOp::LOAD  ? fmt::format("let {} = {}[{}]", result, lhs, rhs)
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
    [[nodiscard]] auto toString() const {
        std::string str;
        str += fmt::format("{}:\n", label);
        for (const auto& inst : insts) {
            str += fmt::format("  {}\n", inst);
        }
        str += exit ? fmt::format("  {}\n", *exit) : fmt::format("  <noexit>\n");
        return str;
    }
    explicit Block(std::string label) : label(std::move(label)) {}

    friend struct gen::Generator;

private:
    std::vector<Inst> insts;
    std::optional<Exit> exit;  // construct Block first, then assign exit instrument.
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
    const ast::ConstInitVal* init;
    SIMPLE_TO_STRING(init ? fmt::format("var {}: {} = {}", var, var.type, *init)
                          : fmt::format("var {}: {}", var, var.type));
};

struct Func {
    const Type type;
    const std::string name;
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

    auto newTemp(const Type& type) -> TempValue {
        return TempValue{.type = type, .id = temp_count++};
    }

    auto newBlock(const std::string& label) -> Block* {
        blocks.emplace_back(label);
        return &blocks.back();
    }

    auto newBlock() -> Block* {
        return newBlock(fmt::format(".L{}", temp_label_count++));
    }

    auto entrance() -> Block* {
        return &blocks.front();
    }

    void addAlloc(const Alloc& alloc) {
        locals.push_back(alloc);
    }

    Func(Type type, std::string name) : type(std::move(type)), name(std::move(name)) {
        newBlock(".entry");
    }

    void pushLoop(const Block* continue_target, const Block* break_target) {
        loops.push_back(LoopContext{continue_target, break_target});
    }
    void popLoop() {
        loops.pop_back();
    }
    [[nodiscard]] auto& currentLoop() const {
        if (loops.empty()) {
            throw CompilerError("No loop context available");
        }
        return loops.back();
    }

private:
    std::vector<Alloc> locals;
    std::vector<Block> blocks;
    struct LoopContext {
        const Block* continue_target;
        const Block* break_target;
    };
    std::vector<LoopContext> loops;  // for break/continue target
    size_t temp_count{0};
    size_t temp_label_count{0};
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

    Program(const ast::SemanticAST& ast) : ast(ast) {}
    const ast::SemanticAST& ast;
};

}  // namespace ir