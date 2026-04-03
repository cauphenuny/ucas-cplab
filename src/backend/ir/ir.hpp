#pragma once

#include "frontend/ast/analysis/semantic_ast.h"
#include "op.hpp"
#include "type.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <variant>

namespace ir {

using Type = adt::TypeBox;

struct Block;
struct Func;
namespace gen {
struct Generator;
}
namespace vm {
struct VirtualMachine;
}

struct NamedValue {
    Type type;
    ast::SymDefNode def;

    friend bool operator==(const NamedValue& lhs, const NamedValue& rhs) {
        return lhs.def == rhs.def;
    }

    SIMPLE_TO_STRING(match(
        def, [&](const ast::FuncDef* f) { return f->name; },
        [&](auto v) { return fmt::format("{}_{}", v->name, v->loc); }));
};

struct TempValue {
    Type type;
    size_t id;

    SIMPLE_TO_STRING(fmt::format("${}", id));
};

struct ConstexprValue;

inline auto serializeArray(const Type& type, std::byte* buffer) -> std::string {
    std::string result;
    auto elem_type = type.as<adt::Array>().elem;
    auto size = type.as<adt::Array>().size;
    for (size_t i = 0; i < size; i++) {
        result += match(
            elem_type.var(),
            [&](const adt::Primitive& prim) -> std::string {
                return match(prim, [&](auto v) {
                    using type = typename decltype(v)::type;
                    return fmt::format("{}", *(type*)buffer);
                });
            },
            [&](const adt::Array& arr) -> std::string { return serializeArray(elem_type, buffer); },
            [&](const auto&) -> std::string {
                throw COMPILER_ERROR(
                    fmt::format("Unsupported type in ConstexprValue array: {}", elem_type));
            }) + ", ";
        buffer += adt::size_of(elem_type);
    }
    if (result.size())        result.pop_back(), result.pop_back();
    return "{" + result + "}";
}

struct ConstexprValue {
    Type type;
    std::variant<std::monostate, int, float, bool, double, std::unique_ptr<std::byte[]>> val;

    SIMPLE_TO_STRING(match(
                         val, [&](std::monostate) { return std::string(""); },
                         [&](const std::unique_ptr<std::byte[]>& vec) -> std::string {
                             return serializeArray(type, vec.get());
                         },
                         [&](auto v) { return fmt::format("{}", v); }););

    ConstexprValue() : type(adt::construct<void>()), val(std::monostate{}) {}
    template <typename T> ConstexprValue(T v) : type(adt::construct<T>()), val(v) {}
    ConstexprValue(Type type, std::unique_ptr<std::byte[]> vec)
        : type(std::move(type)), val(std::move(vec)) {}
};

using LeftValue = std::variant<NamedValue, TempValue>;
using Value = std::variant<ConstexprValue, LeftValue>;

inline LeftValue as_lvalue(const Value& value) {
    return match(
        value, [&](const LeftValue& val) -> LeftValue { return val; },
        [&](const ConstexprValue& c) -> LeftValue {
            throw COMPILER_ERROR(fmt::format("expected LeftValue, got {}", c));
        });
}

inline auto type(const LeftValue& value) -> Type {
    return match(value, [&](const auto& var) { return var.type; });
}

inline auto type(const ConstexprValue& value) -> Type {
    return value.type;
}

inline auto type(const Value& value) -> Type {
    return match(value, [&](const auto& val) { return type(val); });
}

struct UnaryInst {
    UnaryInstOp op;
    LeftValue result;
    Value operand;

    SIMPLE_TO_STRING(fmt::format("{}: {} = {}{}", result, type(result), op, operand))
};

struct BinaryInst {
    InstOp op;
    LeftValue result;
    Value lhs, rhs;

    SIMPLE_TO_STRING(op == InstOp::LOAD
                         ? fmt::format("{}: {} = {}[{}]", result, type(result), lhs, rhs)
                     : op == InstOp::STORE
                         ? fmt::format("{}[{}] = {}", result, lhs, rhs)
                         : fmt::format("{}: {} = {} {} {}", result, type(result), lhs, op, rhs))
};

struct CallInst {
    LeftValue result;
    NamedValue func;
    std::vector<Value> args;

    [[nodiscard]] auto toString() const {
        std::string arg_str;
        for (auto&& arg : args) {
            arg_str += fmt::format("{}, ", arg);
        }
        if (!arg_str.empty()) arg_str.pop_back(), arg_str.pop_back();
        return fmt::format("{}: {} = {}({})", result, type(result), func, arg_str);
    }
};

using Inst = std::variant<UnaryInst, BinaryInst, CallInst>;

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
    JumpExit(const Block* target) : target(target) {
        if (!target) {
            throw COMPILER_ERROR("target block cannot be null");
        }
    }
    [[nodiscard]] auto toString() const -> std::string;
};

using Exit = std::variant<BranchExit, JumpExit, ReturnExit>;

struct Block {
    const std::string label;
    Block(Block&&) =
        delete;  // NOTE: Block is not movable because some instructions may hold references to it.

    [[nodiscard]] auto toString() const {
        std::string str;
        str += fmt::format("{}:\n", label);
        for (const auto& inst : insts) {
            str += fmt::format("  {}\n", inst);
        }
        str += exit ? fmt::format("  {}\n", *exit) : fmt::format("  <noexit>\n");
        return str;
    }
    void add(Inst inst) {
        insts.push_back(std::move(inst));
    }

    friend struct gen::Generator;
    friend struct Func;
    friend struct vm::VirtualMachine;

    Block(std::string label, std::vector<Inst> insts, Exit exit)
        : label(std::move(label)), insts(std::move(insts)), exit(std::move(exit)) {}
    explicit Block(std::string label) : label(std::move(label)) {}

private:
    std::vector<Inst> insts;
    std::optional<Exit> exit;  // construct Block first, then assign exit instrument.
};

inline auto BranchExit::toString() const -> std::string {
    return fmt::format("branch {} ? {} : {}", cond, true_target ? true_target->label : "<unknown>",
                       false_target ? false_target->label : "<unknown>");
}

inline auto JumpExit::toString() const -> std::string {
    return fmt::format("jump {}", target ? target->label : "<unknown>");
}

struct Alloc {
    NamedValue var;
    std::optional<ConstexprValue> init;
    SIMPLE_TO_STRING(init ? fmt::format("let {}: {} = {}", var, var.type, init)
                          : fmt::format("let {}: {}", var, var.type));
};

struct Func {
    const Type ret_type;
    const std::string name;
    const std::vector<NamedValue> params;

    Func(Type ret_type, std::string name, std::vector<NamedValue> params = {})
        : ret_type(std::move(ret_type)), name(std::move(name)), params(std::move(params)) {
        newBlock(".entry");
    }

    [[nodiscard]] auto toString() const {
        std::string params = "";
        for (const auto& param : this->params) {
            params += fmt::format("{}: {}, ", param, param.type);
        }
        if (!params.empty()) params.pop_back(), params.pop_back();

        std::string str;
        if (ret_type == adt::construct<void>()) {
            str += fmt::format("fn {}({}):\n", name, params);
        } else {
            str += fmt::format("fn {}({}) -> {}:\n", name, params, ret_type);
        }
        for (const auto& def : locals_) {
            str += fmt::format("  {}\n", def);
        }
        for (const auto& block : blocks_) {
            str += fmt::format("{}", *block);
        }
        return str;
    }

    [[nodiscard]] const auto& locals() const {
        return locals_;
    }

    [[nodiscard]] const auto& blocks() const {
        return blocks_;
    }

    [[nodiscard]] const auto& temps() const {
        return temps_;
    }

    auto newTemp(const Type& type) -> TempValue {
        auto temp = TempValue{.type = type, .id = temps_.size()};
        temps_.push_back(type);
        return temp;
    }

    auto newBlock(const std::string& label) -> Block* {
        blocks_.emplace_back(std::make_unique<Block>(label));
        return blocks_.back().get();
    }

    auto newBlock() -> Block* {
        return newBlock(fmt::format(".L{}", temp_label_count++));
    }

    auto entrance() -> Block* {
        return blocks_.front().get();
    }

    void addLocal(Alloc alloc) {
        locals_.push_back(std::move(alloc));
    }

    void pushLoop(const Block* continue_target, const Block* break_target) {
        loops.push_back(LoopContext{continue_target, break_target});
    }
    void popLoop() {
        loops.pop_back();
    }
    [[nodiscard]] auto& currentLoop() const {
        if (loops.empty()) {
            throw COMPILER_ERROR("No loop context available");
        }
        return loops.back();
    }

    Func(Func&&) = default;

private:
    std::vector<Alloc> locals_;
    std::vector<Type> temps_;
    std::vector<std::unique_ptr<Block>> blocks_;
    struct LoopContext {
        const Block* continue_target;
        const Block* break_target;
    };
    std::vector<LoopContext> loops;  // for break/continue target
    size_t temp_label_count{0};
};

struct Program {
    [[nodiscard]] auto toString() const {
        std::string str;
        for (const auto& def : globals) {
            str += fmt::format("{}\n", def);
        }
        if (!str.empty()) str += "\n";
        for (const auto& func : funcs) {
            str += fmt::format("{}\n", func);
        }
        str.pop_back(), str.pop_back();  // remove last newlines
        return str;
    }

    Program(const ast::SemanticAST& ast) : ast(ast) {}
    const ast::SemanticAST& ast;

    void addFunc(Func func) {
        funcs.push_back(std::move(func));
    }
    void addGlobal(Alloc alloc) {
        globals.push_back(std::move(alloc));
    }

    [[nodiscard]] const Func& findFunc(const std::string& name) const {
        for (const auto& func : funcs) {
            if (func.name == name) {
                return func;
            }
        }
        throw COMPILER_ERROR(fmt::format("function '{}' not found", name));
    }

    friend struct vm::VirtualMachine;

private:
    std::vector<Alloc> globals;
    std::vector<Func> funcs;
};

}  // namespace ir

template <> struct std::hash<ir::NamedValue> : std::hash<ast::SymDefNode> {
    auto operator()(const ir::NamedValue& v) const noexcept -> std::size_t {
        return std::hash<ast::SymDefNode>{}(v.def);
    }
};