#pragma once

#include "op.hpp"
#include "type.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <variant>
#include <cstring>

namespace ir {

using Type = adt::TypeBox;

struct Block;
struct Alloc;
struct Func;
struct BuiltinFunc;

namespace gen {
struct Generator;
}
namespace vm {
struct VirtualMachine;
}

using NameDef = std::variant<const Alloc*, const Func*, const BuiltinFunc*>;

struct NamedValue {
    Type type;
    NameDef def;

    friend bool operator==(const NamedValue& lhs, const NamedValue& rhs) {
        return lhs.def == rhs.def;
    }

    [[nodiscard]] auto toString() const -> std::string;
};

struct TempValue {
    Type type;
    size_t id;

    SIMPLE_TO_STRING(fmt::format("${}", id));
};

struct ConstexprValue;

inline auto toString(int val) -> std::string {
    return fmt::format("{}", val);
}
inline auto toString(float val) -> std::string {
    return fmt::format("{:#g}f", val);
}
inline auto toString(double val) -> std::string {
    return fmt::format("{:#g}", val);
}
inline auto toString(bool val) -> std::string {
    return val ? "true" : "false";
}

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
                              return toString(*(type*)buffer);
                          });
                      },
                      [&](const adt::Array& arr) -> std::string {
                          return serializeArray(elem_type, buffer);
                      },
                      [&](const auto&) -> std::string {
                          throw COMPILER_ERROR(fmt::format(
                              "Unsupported type in ConstexprValue array: {}", elem_type));
                      }) +
                  ", ";
        buffer += adt::size_of(elem_type);
    }
    if (result.size()) result.pop_back(), result.pop_back();
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
                         [&](auto v) { return ir::toString(v); }););

    ConstexprValue() : type(adt::construct<void>()), val(std::monostate{}) {}
    template <typename T> ConstexprValue(T v) : type(adt::construct<T>()), val(v) {}
    ConstexprValue(Type type, std::unique_ptr<std::byte[]> vec)
        : type(std::move(type)), val(std::move(vec)) {}

    ConstexprValue(const ConstexprValue& other) : type(other.type) {
        match(
            other.val, [&](std::monostate) { val = std::monostate{}; },
            [&](const std::unique_ptr<std::byte[]>& vec) {
                if (vec) {
                    auto size = adt::size_of(type);
                    auto buf = std::make_unique<std::byte[]>(size);
                    std::memcpy(buf.get(), vec.get(), size);
                    val = std::move(buf);
                } else {
                    val = std::unique_ptr<std::byte[]>{};
                }
            },
            [&](auto v) { val = v; });
    }
    ConstexprValue(ConstexprValue&&) noexcept = default;
    ConstexprValue& operator=(const ConstexprValue& other) {
        if (this != &other) {
            *this = ConstexprValue(other);
        }
        return *this;
    }
    ConstexprValue& operator=(ConstexprValue&&) noexcept = default;
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

    SIMPLE_TO_STRING(fmt::format("{}: {} = {}{};", result, type(result), op, operand))
};

struct BinaryInst {
    InstOp op;
    LeftValue result;
    Value lhs, rhs;

    SIMPLE_TO_STRING(op == InstOp::LOAD
                         ? fmt::format("{}: {} = {}[{}];", result, type(result), lhs, rhs)
                     : op == InstOp::STORE
                         ? fmt::format("{}[{}] = {};", result, lhs, rhs)
                         : fmt::format("{}: {} = {} {} {};", result, type(result), lhs, op, rhs))
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
        return fmt::format("{}: {} = {}({});", result, type(result), func, arg_str);
    }
};

using Inst = std::variant<UnaryInst, BinaryInst, CallInst>;

struct ReturnExit {
    Value exp;

    SIMPLE_TO_STRING(fmt::format("return {};", exp));
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
        str += fmt::format(".{}:\n", label);
        for (const auto& inst : insts_) {
            str += fmt::format("  {}\n", inst);
        }
        str += exit_ ? fmt::format("  {}\n", *exit_) : fmt::format("  <noexit>\n");
        return str;
    }
    void add(Inst inst) {
        insts_.push_back(std::move(inst));
    }
    void setExit(Exit exit) {
        if (this->exit_) {
            throw COMPILER_ERROR(fmt::format("Block {} already has an exit instruction", label));
        }
        this->exit_ = std::move(exit);
    }
    [[nodiscard]] const auto& insts() const {
        return insts_;
    }
    [[nodiscard]] const auto& exit() const {
        return exit_;
    }

    Block(std::string label, std::vector<Inst> insts, Exit exit)
        : label(std::move(label)), insts_(std::move(insts)), exit_(std::move(exit)) {}
    explicit Block(std::string label) : label(std::move(label)) {}

private:
    std::vector<Inst> insts_;
    std::optional<Exit> exit_;  // construct Block first, then assign exit instruction.
};

inline auto BranchExit::toString() const -> std::string {
    return fmt::format("branch {} ? {} : {};", cond, true_target ? true_target->label : "<unknown>",
                       false_target ? false_target->label : "<unknown>");
}

inline auto JumpExit::toString() const -> std::string {
    return fmt::format("jump {};", target ? target->label : "<unknown>");
}

// NOTE: non-comptime Alloc is always mutable, for immutable value, use TempValue
struct Alloc {
    std::string name;
    Type type;
    bool comptime{false};   // value known at compile time
    std::optional<ConstexprValue> init;

    [[nodiscard]] std::string toString() const {
        auto keyword = comptime ? "const" : "let";
        if (init) {
            return fmt::format("{} {}: {} = {};", keyword, name, type, init);
        }
        return fmt::format("{} {}: {};", keyword, name, type);
    }

    Alloc(Alloc&&) = delete;
    Alloc(std::string name, Type type, bool comptime = false,
          std::optional<ConstexprValue> init = std::nullopt)
        : name(std::move(name)), type(std::move(type)), comptime(comptime),
          init(std::move(init)) {}
};

struct Func {
    const Type ret_type;
    const std::string name;
    const std::vector<std::unique_ptr<Alloc>> params;

    Func(Type ret_type, std::string name, std::vector<std::unique_ptr<Alloc>> params = {})
        : ret_type(std::move(ret_type)), name(std::move(name)), params(std::move(params)) {}
    Func(Func&&) = delete;

    [[nodiscard]] auto toString() const {
        std::string params = "";
        for (const auto& param : this->params) {
            params += fmt::format("{}: {}, ", param->name, param->type);
        }
        if (!params.empty()) params.pop_back(), params.pop_back();

        std::string str;
        if (ret_type == adt::construct<void>()) {
            str += fmt::format("fn {}({}) {{\n", name, params);
        } else {
            str += fmt::format("fn {}({}) -> {} {{\n", name, params, ret_type);
        }
        for (const auto& def : locals_) {
            str += fmt::format("  {}\n", def);
        }
        for (const auto& block : blocks_) {
            str += fmt::format("{}", *block);
        }
        str += "}\n";
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
        return newBlock(fmt::format("L{}", temp_label_count++));
    }

    auto entrance() -> Block* {
        return blocks_.front().get();
    }

    void addLocal(std::unique_ptr<Alloc> alloc) {
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

private:
    std::vector<std::unique_ptr<Alloc>> locals_;
    std::vector<Type> temps_;
    std::vector<std::unique_ptr<Block>> blocks_;
    struct LoopContext {
        const Block* continue_target;
        const Block* break_target;
    };
    std::vector<LoopContext> loops;  // for break/continue target
    size_t temp_label_count{0};
};

struct BuiltinFunc {
    std::string name;
    Type type;
    BuiltinFunc(BuiltinFunc&&) = delete;
    BuiltinFunc(std::string name, Type type) : name(std::move(name)), type(std::move(type)) {}
};

inline auto NamedValue::toString() const -> std::string {
    return match(def, [&](const auto* def) { return def->name; });
}

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
        while (!str.empty() && str.back() == '\n') str.pop_back();  // remove last newlines
        return str;
    }

    void addFunc(std::unique_ptr<Func> func) {
        funcs.push_back(std::move(func));
    }
    void addGlobal(std::unique_ptr<Alloc> alloc) {
        globals.push_back(std::move(alloc));
    }
    void addBuiltinFunc(std::unique_ptr<BuiltinFunc> func) {
        builtin_funcs.push_back(std::move(func));
    }

    [[nodiscard]] const Func& findFunc(const std::string& name) const {
        for (const auto& func : funcs) {
            if (func->name == name) {
                return *func;
            }
        }
        throw COMPILER_ERROR(fmt::format("function '{}' not found", name));
    }

    friend struct vm::VirtualMachine;
    friend class IRConstructVisitor;

    [[nodiscard]] const auto& getGlobals() const {
        return globals;
    }
    [[nodiscard]] const auto& getFuncs() const {
        return funcs;
    }
    [[nodiscard]] const auto& getBuiltinFuncs() const {
        return builtin_funcs;
    }

private:
    std::vector<std::unique_ptr<Alloc>> globals;
    std::vector<std::unique_ptr<Func>> funcs;
    std::vector<std::unique_ptr<BuiltinFunc>> builtin_funcs;
};

}  // namespace ir

template <> struct std::hash<ir::NamedValue> : std::hash<ir::NameDef> {
    auto operator()(const ir::NamedValue& v) const noexcept -> std::size_t {
        return std::hash<ir::NameDef>{}(v.def);
    }
};