#pragma once

#include "op.hpp"
#include "type.hpp"

#include <cstddef>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ir {

using Type = ir::type::TypeBox;

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

inline auto ind(size_t level) {  // indent
    return std::string(level * 4, ' ');
}

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
    const Func* func;

    friend bool operator==(const TempValue& lhs, const TempValue& rhs) {
        return lhs.func == rhs.func && lhs.id == rhs.id;
    }

    SIMPLE_TO_STRING(fmt::format("${}", id));
};

struct ConstexprValue;

auto toString(int val) -> std::string;
auto toString(float val) -> std::string;
auto toString(double val) -> std::string;
auto toString(bool val) -> std::string;

auto serializeArray(const Type& type, std::byte* buffer) -> std::string;

struct ConstexprValue {
    Type type;
    std::variant<std::monostate, int, float, bool, double, std::unique_ptr<std::byte[]>> val;

    SIMPLE_TO_STRING(match(
                         val, [&](std::monostate) { return std::string(""); },
                         [&](const std::unique_ptr<std::byte[]>& vec) -> std::string {
                             return serializeArray(type, vec.get());
                         },
                         [&](auto v) { return ir::toString(v); }););

    ConstexprValue() : type(ir::type::construct<void>()), val(std::monostate{}) {}
    template <typename T> ConstexprValue(T v) : type(ir::type::construct<T>()), val(v) {}
    ConstexprValue(Type type, std::unique_ptr<std::byte[]> vec)
        : type(std::move(type)), val(std::move(vec)) {}

    ConstexprValue(const ConstexprValue& other);
    ConstexprValue(ConstexprValue&&) noexcept = default;
    ConstexprValue& operator=(const ConstexprValue& other);
    ConstexprValue& operator=(ConstexprValue&&) noexcept = default;

    static ConstexprValue zeros_like(const Type& type);

    friend bool operator==(const ConstexprValue& lhs, const ConstexprValue& rhs);
};

struct SSAValue {
    Type type;
    const Alloc* def;
    size_t version;

    friend bool operator==(const SSAValue& lhs, const SSAValue& rhs) {
        return lhs.def == rhs.def && lhs.version == rhs.version;
    }
    [[nodiscard]] auto toString() const -> std::string;
    SSAValue(Type type, const Alloc* def, size_t version)
        : type(std::move(type)), def(def), version(version) {}
};

using LeftValue = std::variant<NamedValue, TempValue, SSAValue>;
using Value = std::variant<ConstexprValue, LeftValue>;

LeftValue as_lvalue(const Value& value);
auto type_of(const LeftValue& value) -> Type;
auto type_of(const ConstexprValue& value) -> Type;
auto type_of(const Value& value) -> Type;

struct UnaryInst {
    UnaryInstOp op;
    LeftValue result;
    Value operand;

    SIMPLE_TO_STRING(op == UnaryInstOp::LOAD
                         ? fmt::format("{}: {} = *({});", result, type_of(result), operand)
                     : op == UnaryInstOp::STORE
                         ? fmt::format("*({}) = {};", result, operand)
                         : fmt::format("{}: {} = {}{};", result, type_of(result), op, operand))
};

struct BinaryInst {
    InstOp op;
    LeftValue result;
    Value lhs, rhs;

    SIMPLE_TO_STRING(op == InstOp::LOAD
                         ? fmt::format("{}: {} = {}[{}];", result, type_of(result), lhs, rhs)
                     : op == InstOp::STORE
                         ? fmt::format("{}[{}] = {};", result, lhs, rhs)
                         : fmt::format("{}: {} = {} {} {};", result, type_of(result), lhs, op, rhs))
};

struct CallInst {
    LeftValue result;
    NamedValue func;
    std::vector<Value> args;

    [[nodiscard]] auto toString() const -> std::string;
};

struct PhiInst {
    LeftValue result;
    std::unordered_map<Block*, Value> args;

    [[nodiscard]] auto toString() const -> std::string;
};

using Inst = std::variant<PhiInst, UnaryInst, BinaryInst, CallInst>;

struct ReturnExit {
    Value exp;

    SIMPLE_TO_STRING(fmt::format("return {};", exp));
};

struct BranchExit {
    Value cond;
    Block *true_target, *false_target;
    [[nodiscard]] auto toString() const -> std::string;
};

struct JumpExit {
    Block* target;
    JumpExit(Block* target);
    [[nodiscard]] auto toString() const -> std::string;
};

using Exit = std::variant<BranchExit, JumpExit, ReturnExit>;

struct Block {
    std::string label;
    Block(Block&&) =
        delete;  // NOTE: Block is not movable because some instructions may hold references to it.

    [[nodiscard]] auto toString() const -> std::string;

    void add(Inst inst);
    void prepend(Inst inst);
    auto pop_front() -> Inst;

    void setExit(Exit exit);
    [[nodiscard]] bool hasExit() const;
    [[nodiscard]] const Exit& exit() const;
    [[nodiscard]] Exit& exit();

    [[nodiscard]] const auto& insts() const {
        return insts_;
    }
    [[nodiscard]] auto& insts() {
        return insts_;
    }

    auto split(std::list<Inst>::iterator next_start, Exit prev_exit, std::string next_label)
        -> std::unique_ptr<Block>;

    auto clone(const std::string& prefix) -> std::unique_ptr<Block>;

    Block(std::string label, std::list<Inst> insts, Exit exit);
    explicit Block(std::string label);

private:
    std::list<Inst> insts_;
    std::optional<Exit> exit_;  // construct Block first, then assign exit instruction.
};

// NOTE: non-comptime Alloc is always mutable, for immutable value, use TempValue
struct Alloc {
    std::string name;
    Type type;
    bool comptime{false};   // value known at compile time
    bool immutable{false};  // value cannot be assigned multiple times
    bool reference{false};  // value can only accessed by its ref's load/store operation

    std::optional<ConstexprValue> init;

    Alloc(std::string name, Type type, bool comptime = false, bool immutable = false,
          bool reference = false, std::optional<ConstexprValue> init = std::nullopt);

    [[nodiscard]] std::string toString() const;

    Alloc(Alloc&&) = delete;
    [[nodiscard]] std::unique_ptr<Alloc> clone() const;

    static auto constant(std::string name, Type type, ConstexprValue init)
        -> std::unique_ptr<Alloc>;
    static auto variable(std::string name, Type type,
                         std::optional<ConstexprValue> init = std::nullopt, bool immutable = false)
        -> std::unique_ptr<Alloc>;
    static auto reference_var(std::string name, Type type,
                              std::optional<ConstexprValue> init = std::nullopt,
                              bool immutable = false) -> std::unique_ptr<Alloc>;

    [[nodiscard]] auto value() const -> NamedValue;
};

struct Func {
    const Type ret_type;
    const std::string name;
    const std::vector<std::unique_ptr<Alloc>> params;

    struct LoopContext {
        Block* continue_target;
        Block* break_target;
    };

    Func(Type ret_type, std::string name, std::vector<std::unique_ptr<Alloc>> params = {});
    Func(Func&&) = delete;

    [[nodiscard]] auto toString() const -> std::string;

    [[nodiscard]] const std::vector<std::unique_ptr<Alloc>>& locals() const;
    std::vector<std::unique_ptr<Alloc>>& locals();

    [[nodiscard]] const std::vector<std::unique_ptr<Block>>& blocks() const;
    std::vector<std::unique_ptr<Block>>& blocks();

    [[nodiscard]] const auto& temps() const {
        return temps_;
    }

    TempValue newTemp(const Type& type, Block* container);
    void addLocal(std::unique_ptr<Alloc> alloc);

    Block* newBlock(const std::string& label);
    Block* newBlock();
    void addBlock(std::unique_ptr<Block> block);
    [[nodiscard]] Block* findBlock(const std::string& label) const;

    [[nodiscard]] const Alloc* findAlloc(const std::string& name) const;

    [[nodiscard]] Block* entrance() const;
    [[nodiscard]] std::vector<Block*> exits() const;

    void pushLoop(Block* continue_target, Block* break_target);
    void popLoop();
    [[nodiscard]] const LoopContext& currentLoop() const;

    [[nodiscard]] std::unique_ptr<Func> clone(const std::string& prefix = "") const;

private:
    std::vector<std::unique_ptr<Alloc>> locals_;
    std::vector<std::unique_ptr<Block>> blocks_;

    struct TempInfo {
        Type type;
        Block* block;  // block where the temp is defined
    };
    std::vector<TempInfo> temps_;
    std::vector<LoopContext> loops;  // for break/continue target

    size_t temp_label_count{0};
};

struct BuiltinFunc {
    std::string name;
    Type type;
    BuiltinFunc(BuiltinFunc&&) = delete;
    BuiltinFunc(std::string name, Type type) : name(std::move(name)), type(std::move(type)) {}
};

struct Program {
    [[nodiscard]] auto toString() const -> std::string;

    void addFunc(std::unique_ptr<Func> func);
    void addGlobal(std::unique_ptr<Alloc> alloc);
    void addBuiltinFunc(std::unique_ptr<BuiltinFunc> func);

    [[nodiscard]] const Func& findFunc(const std::string& name) const;

    [[nodiscard]] auto findAlloc(const std::string& name) const -> const Alloc*;

    friend struct vm::VirtualMachine;
    friend class IRConstructVisitor;

    [[nodiscard]] const std::vector<std::unique_ptr<Alloc>>& getGlobals() const;
    [[nodiscard]] const std::vector<std::unique_ptr<Func>>& getFuncs() const;
    std::vector<std::unique_ptr<Func>>& getFuncs();
    [[nodiscard]] const std::vector<std::unique_ptr<BuiltinFunc>>& getBuiltinFuncs() const;

private:
    std::vector<std::unique_ptr<Alloc>> globals;
    std::vector<std::unique_ptr<Func>> funcs;
    std::vector<std::unique_ptr<BuiltinFunc>> builtin_funcs;
};

template <typename T> inline void hash_combine(std::size_t& seed, const T& v) {
    seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

}  // namespace ir

template <typename T1, typename T2> struct std::hash<std::pair<T1, T2>> {
    auto operator()(const std::pair<T1, T2>& p) const noexcept -> std::size_t {
        std::size_t seed = 0;
        ir::hash_combine(seed, p.first);
        ir::hash_combine(seed, p.second);
        return seed;
    }
};

template <> struct std::hash<ir::TempValue> {
    auto operator()(const ir::TempValue& v) const noexcept -> std::size_t {
        return std::hash<std::size_t>{}(v.id);
    }
};

template <> struct std::hash<ir::NamedValue> {
    auto operator()(const ir::NamedValue& v) const noexcept -> std::size_t {
        return std::hash<ir::NameDef>{}(v.def);
    }
};

template <> struct std::hash<ir::SSAValue> {
    auto operator()(const ir::SSAValue& v) const noexcept -> std::size_t {
        std::size_t seed = 0;
        ir::hash_combine(seed, v.def);
        ir::hash_combine(seed, v.version);
        return seed;
    }
};

template <> struct std::hash<ir::ConstexprValue> {
    auto operator()(const ir::ConstexprValue& v) const noexcept -> std::size_t;
    auto hash_array(const ir::Type& elem_type, std::byte* buffer, size_t length) const -> size_t;
};