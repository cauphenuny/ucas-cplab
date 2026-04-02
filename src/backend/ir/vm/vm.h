#include "../ir.hpp"
#include "backend/ir/op.hpp"
#include "backend/ir/type.hpp"
#include "frontend/ast/analysis/semantic_ast.h"
#include "size.hpp"
#include "utils/error.hpp"
#include "view.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace ir::vm {

struct BuiltinFunc;

struct StackFrame {
    std::unordered_map<NamedValue, View> vars;  // NOTE: args is stored in vars
    std::vector<View> temps;
};

struct VirtualMachine {
private:
    template <template <typename> class Op>
    void eval_binary(View& dest, const View& lhs, const View& rhs) const {
        match(dest.type.as<adt::Primitive>(), [&](auto value) {
            using type = typename decltype(value)::type;
            if constexpr (std::is_floating_point_v<type> &&
                          std::is_same_v<Op<type>, std::modulus<type>>) {
                throw CompilerError("Cannot apply modulus operator to floating point");
            } else {
                *(type*)dest.data = Op<type>{}(*(type*)lhs.data, *(type*)rhs.data);
            }
        });
    }

    template <template <typename> class Op> void eval_unary(View& dest, const View& operand) const {
        match(dest.type.as<adt::Primitive>(), [&](auto value) {
            using type = typename decltype(value)::type;
            *(type*)dest.data = Op<type>{}(*(type*)operand.data);
        });
    }

    template <template <typename> class Op>
    void eval_comparison(View& dest, const View& lhs, const View& rhs) const {
        match(lhs.type.as<adt::Primitive>(), [&](auto value) {
            using type = typename decltype(value)::type;
            *(bool*)dest.data = Op<type>{}(*(type*)lhs.data, *(type*)rhs.data);
        });
    }

    void eval(InstOp op, View& dest, const View& lhs, const View& rhs) const {
        auto it = binary_ops.find(op);
        if (it != binary_ops.end()) {
            it->second(dest, lhs, rhs);
        } else {
            throw CompilerError(
                fmt::format("Unsupported binary operation: {}", static_cast<int>(op)));
        }
    }

    void eval(UnaryInstOp op, View& dest, const View& operand) const {
        auto it = unary_ops.find(op);
        if (it != unary_ops.end()) {
            it->second(dest, operand);
        } else {
            throw CompilerError(
                fmt::format("Unsupported unary operation: {}", static_cast<int>(op)));
        }
    }

    template <typename T1, typename T2>
    void assign(const T1& dest_type, std::byte* dest, const T2& src_type,
                const std::byte* src) const {
        throw CompilerError(fmt::format("Cannot assign {} to {}", src_type, dest_type));
    }

    void assign(const adt::Primitive& dest_type, std::byte* dest, const adt::Primitive& src_type,
                const std::byte* src) const;

    void assign(const adt::Sum& dest_type, std::byte* dest, const ir::Type& src_type,
                const std::byte* src) const;

    void assign(const adt::Array& dest_type, std::byte* dest, const adt::Array& src_type,
                const std::byte* src) const;

    void assign(const adt::Product& dest_type, std::byte* dest, const adt::Product& src_type,
                const std::byte* src) const;

    void assign(View dest, const View& src) const;

    void execute(const BinaryInst& inst, const View& lhs, const View& rhs, View& ret) const;
    void execute(const UnaryInst& inst, const View& operand, View& ret) const;
    void execute(const CallInst& inst, const std::vector<View>& srcs, View& ret) const;

    auto execute(const Block& block, StackFrame& frame, View& ret) const -> const Block*;
    void execute(const Func& func, const std::vector<View>& args, View& ret) const;
    void execute(const BuiltinFunc& func, const std::vector<View>& args, View& ret) const;

    [[nodiscard]] auto view_of(const LeftValue& lval, const StackFrame& frame) const -> View {
        auto fn = [](const LeftValue& val, const StackFrame& frame) -> std::optional<View> {
            return match(
                val,
                [&](const NamedValue& var) -> std::optional<View> {
                    auto it = frame.vars.find(var);
                    if (it != frame.vars.end()) return it->second;
                    return std::nullopt;
                },
                [&](const TempValue& temp) -> std::optional<View> {
                    if (temp.id < frame.temps.size()) return frame.temps[temp.id];
                    return std::nullopt;
                });
        };
        auto local = fn(lval, frame);
        if (local) return *local;
        auto global = fn(lval, global_frame);
        if (global) return *global;
        throw CompilerError(fmt::format("Undefined variable: {}", lval));
    }

    [[nodiscard]] auto view_of(const ConstexprValue& c) const -> View {
        return match(c.val, [&](const auto& val) -> const View {
            return View{.data = (std::byte*)&val, .type = c.type};
        });
    }

    [[nodiscard]] auto view_of(const Value& value, const StackFrame& frame) const -> View {
        return match(
            value, [&](const auto& lval) { return view_of(lval, frame); },
            [&](const ConstexprValue& c) { return view_of(c); });
    }

    void alloc(StackFrame& frame, const Alloc& alloc, std::byte* buffer) const;

    DataLayout layout{sizeof(int), sizeof(float), sizeof(double), sizeof(std::byte*)};

    using BinaryOpFunc = std::function<void(View& dest, const View& lhs, const View& rhs)>;
    std::unordered_map<InstOp, BinaryOpFunc> binary_ops;

    using UnaryOpFunc = std::function<void(View& dest, const View& operand)>;
    std::unordered_map<UnaryInstOp, UnaryOpFunc> unary_ops;

    const ast::SemanticAST* ast;
    const Program* program;

    std::istream& input;
    std::ostream& output;

    StackFrame global_frame;

public:
    int execute(const Program& program);

    VirtualMachine(std::istream& input, std::ostream& output) : input(input), output(output) {
        unary_ops[UnaryInstOp::MOV] = [this](View& dest, const View& operand) {
            std::memcpy(dest.data, operand.data, layout.size_of(dest.type));
        };
        unary_ops[UnaryInstOp::NOT] = [this](View& dest, const View& operand) {
            eval_unary<std::logical_not>(dest, operand);
        };
        unary_ops[UnaryInstOp::NEG] = [this](View& dest, const View& operand) {
            eval_unary<std::negate>(dest, operand);
        };

        binary_ops[InstOp::ADD] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_binary<std::plus>(dest, lhs, rhs);
        };
        binary_ops[InstOp::SUB] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_binary<std::minus>(dest, lhs, rhs);
        };
        binary_ops[InstOp::MUL] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_binary<std::multiplies>(dest, lhs, rhs);
        };
        binary_ops[InstOp::DIV] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_binary<std::divides>(dest, lhs, rhs);
        };
        binary_ops[InstOp::MOD] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_binary<std::modulus>(dest, lhs, rhs);
        };
        binary_ops[InstOp::LT] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_comparison<std::less>(dest, lhs, rhs);
        };
        binary_ops[InstOp::GT] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_comparison<std::greater>(dest, lhs, rhs);
        };
        binary_ops[InstOp::LEQ] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_comparison<std::less_equal>(dest, lhs, rhs);
        };
        binary_ops[InstOp::GEQ] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_comparison<std::greater_equal>(dest, lhs, rhs);
        };
        binary_ops[InstOp::EQ] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_comparison<std::equal_to>(dest, lhs, rhs);
        };
        binary_ops[InstOp::NEQ] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_comparison<std::not_equal_to>(dest, lhs, rhs);
        };
        binary_ops[InstOp::AND] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_binary<std::logical_and>(dest, lhs, rhs);
        };
        binary_ops[InstOp::OR] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_binary<std::logical_or>(dest, lhs, rhs);
        };
        binary_ops[InstOp::LOAD] = [this](View& dest, const View& lhs, const View& rhs) {
            // NOTE: lhs: pointer, rhs: offset
            using namespace adt;
            if (!rhs.type.is<Primitive>() ||
                !std::holds_alternative<Int>(rhs.type.as<Primitive>())) {
                throw CompilerError(fmt::format("Offset must be an integer, but got {}", rhs.type));
            }
            if (!lhs.type.is<Pointer>() && !lhs.type.is<Array>()) {
                throw CompilerError(
                    fmt::format("Expected pointer or array type, but got {}", lhs.type));
            }
            auto offset = *(int*)rhs.data;
            auto elem_type =
                lhs.type.is<Pointer>() ? lhs.type.as<Pointer>().elem : lhs.type.as<Array>().elem;
            dest.data = lhs.data + offset * layout.size_of(elem_type);
        };
        binary_ops[InstOp::STORE] = [this](View& dest, const View& lhs, const View& rhs) {
            // NOTE: dest: pointer, lhs: offset, rhs: value
            using namespace adt;
            if (!lhs.type.is<Primitive>() ||
                !std::holds_alternative<Int>(lhs.type.as<Primitive>())) {
                throw CompilerError(fmt::format("Offset must be an integer, but got {}", lhs.type));
            }
            if (!dest.type.is<Pointer>() && !dest.type.is<Array>()) {
                throw CompilerError(
                    fmt::format("Expected pointer or array type, but got {}", dest.type));
            }
            auto offset = *(int*)lhs.data;
            auto elem_type =
                dest.type.is<Pointer>() ? dest.type.as<Pointer>().elem : dest.type.as<Array>().elem;
            assign(elem_type, dest.data + offset * layout.size_of(elem_type), rhs.type, rhs.data);
        };
    }
};

struct BuiltinFunc {
    std::function<void(View& ret, const std::vector<View>& args, std::istream& input,
                       std::ostream& output)>
        apply;
    BuiltinFunc(std::function<void(View& ret, const std::vector<View>& args, std::istream& input,
                                   std::ostream& output)>
                    apply)
        : apply(std::move(apply)) {}
};

inline const std::unordered_map<std::string, BuiltinFunc> builtin_funcs = {
    {"get_int", BuiltinFunc{[](View& ret, const std::vector<View>& args, std::istream& input,
                               std::ostream& output) {
         int value;
         input >> value;
         std::memcpy(ret.data, &value, sizeof(int));
     }}},
    {"get_float", BuiltinFunc{[](View& ret, const std::vector<View>& args, std::istream& input,
                                 std::ostream& output) {
         float value;
         input >> value;
         std::memcpy(ret.data, &value, sizeof(float));
     }}},
    {"get_double", BuiltinFunc{[](View& ret, const std::vector<View>& args, std::istream& input,
                                  std::ostream& output) {
         double value;
         input >> value;
         std::memcpy(ret.data, &value, sizeof(double));
     }}},
    {"print_int", BuiltinFunc{[](View& ret, const std::vector<View>& args, std::istream& input,
                                 std::ostream& output) {
         int value = *(int*)args[0].data;
         output << value << '\n';
     }}},
    {"print_float", BuiltinFunc{[](View& ret, const std::vector<View>& args, std::istream& input,
                                   std::ostream& output) {
         float value = *(float*)args[0].data;
         output << value << '\n';
     }}},
    {"print_double", BuiltinFunc{[](View& ret, const std::vector<View>& args, std::istream& input,
                                    std::ostream& output) {
         double value = *(double*)args[0].data;
         output << value << '\n';
     }}},
    {"print_bool", BuiltinFunc{[](View& ret, const std::vector<View>& args, std::istream& input,
                                  std::ostream& output) {
         bool value = *(bool*)args[0].data;
         output << (value ? "true" : "false") << '\n';
     }}},
};

}  // namespace ir::vm