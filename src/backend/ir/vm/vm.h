#include "../ir.h"
#include "backend/ir/op.hpp"
#include "backend/ir/type.hpp"
#include "utils/diagnosis.hpp"
#include "view.hpp"

#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace ir::vm {

struct BuiltinFunc;

struct StackFrame {
    std::unordered_map<NameDef, View> vars;  // NOTE: args is stored in vars
    std::vector<View> temps;
};

struct VirtualMachine {
private:
    template <template <typename> class Op>
    void eval_binary(View& dest, const View& lhs, const View& rhs) const {
        using namespace ir::type;
        Primitive dtype = dest.type.is<Reference>() ? Primitive{Int()} : dest.type.as<Primitive>();
        Primitive ltype = lhs.type.is<Reference>() ? Primitive{Int()} : lhs.type.as<Primitive>();
        Primitive rtype = rhs.type.is<Reference>() ? Primitive{Int()} : rhs.type.as<Primitive>();
        Match{dtype, ltype, rtype}([&](auto d, auto l, auto r) {
            using dtype = typename decltype(d)::type;
            using ltype = typename decltype(l)::type;
            using rtype = typename decltype(r)::type;
            if constexpr (std::is_floating_point_v<dtype> &&
                          std::is_same_v<Op<dtype>, std::modulus<dtype>>) {
                *(dtype*)dest.data = std::fmod(*(ltype*)lhs.data, *(rtype*)rhs.data);
            } else {
                *(dtype*)dest.data = Op<dtype>{}(*(ltype*)lhs.data, *(rtype*)rhs.data);
            }
        });
    }

    template <template <typename> class Op> void eval_unary(View& dest, const View& operand) const {
        using namespace ir::type;
        auto otype = operand.type.is<Reference>() ? Primitive{Int()} : operand.type.as<Primitive>();
        auto dtype = dest.type.is<Reference>() ? Primitive{Int()} : dest.type.as<Primitive>();
        Match{dtype, otype}([&](auto d, auto o) {
            using dtype = typename decltype(d)::type;
            using otype = typename decltype(o)::type;
            *(dtype*)dest.data = Op<dtype>{}(*(otype*)operand.data);
        });
    }

    template <template <typename> class Op>
    void eval_comparison(View& dest, const View& lhs, const View& rhs) const {
        using namespace ir::type;
        Primitive dtype = dest.type.is<Reference>() ? Primitive{Int()} : dest.type.as<Primitive>();
        Primitive ltype = lhs.type.is<Reference>() ? Primitive{Int()} : lhs.type.as<Primitive>();
        Primitive rtype = rhs.type.is<Reference>() ? Primitive{Int()} : rhs.type.as<Primitive>();
        Match{dtype, ltype, rtype}([&](auto d, auto l, auto r) {
            using dtype = typename decltype(d)::type;
            using ltype = typename decltype(l)::type;
            using rtype = typename decltype(r)::type;
            using type = std::common_type_t<ltype, rtype>;
            bool result = Op<type>{}((type)(*(ltype*)lhs.data), (type)(*(rtype*)rhs.data));
            *(dtype*)dest.data = result;
        });
    }

    template <typename T> struct shift_left {
        T operator()(T a, T b) const {
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                return a << b;
            } else {
                throw COMPILER_ERROR("shift operations are only defined for integral types");
            }
        }
    };
    template <typename T> struct shift_right_arithmetic {
        T operator()(T a, T b) const {
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                return a >> b;
            } else {
                throw COMPILER_ERROR("shift operations are only defined for integral types");
            }
        }
    };
    template <typename T> struct shift_right_logical {
        T operator()(T a, T b) const {
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                return static_cast<T>(static_cast<std::make_unsigned_t<T>>(a) >> b);
            } else {
                throw COMPILER_ERROR("shift operations are only defined for integral types");
            }
        }
    };

    void eval(InstOp op, View& dest, const View& lhs, const View& rhs) const {
        auto it = binary_ops.find(op);
        if (it != binary_ops.end()) {
            it->second(dest, lhs, rhs);
        } else {
            throw COMPILER_ERROR(
                fmt::format("Unsupported binary operation: {}", static_cast<int>(op)));
        }
    }

    void eval(UnaryInstOp op, View& dest, const View& operand) const {
        auto it = unary_ops.find(op);
        if (it != unary_ops.end()) {
            it->second(dest, operand);
        } else {
            throw COMPILER_ERROR(
                fmt::format("Unsupported unary operation: {}", static_cast<int>(op)));
        }
    }

    template <typename T1, typename T2>
    void assign(const T1& dest_type, std::byte* dest, const T2& src_type,
                const std::byte* src) const {
        throw COMPILER_ERROR(fmt::format("Cannot assign {} to {}", src_type, dest_type));
    }

    void assign(ir::type::Int, std::byte* dest, ir::type::Int32, const std::byte* src) const;
    void assign(ir::type::Int, std::byte* dest, ir::type::Int1, const std::byte* src) const;
    void assign(ir::type::Int, std::byte* dest, const ir::type::Reference&,
                const std::byte* src) const;
    void assign(ir::type::Int32, std::byte* dest, ir::type::Int, const std::byte* src) const;
    void assign(ir::type::Int1, std::byte* dest, ir::type::Int, const std::byte* src) const;
    void assign(const ir::type::Reference&, std::byte* dest, ir::type::Int,
                const std::byte* src) const;

    void assign(ir::type::Float, std::byte* dest, ir::type::Float32, const std::byte* src) const;
    void assign(ir::type::Float, std::byte* dest, ir::type::Float64, const std::byte* src) const;
    void assign(ir::type::Float32, std::byte* dest, ir::type::Float, const std::byte* src) const;
    void assign(ir::type::Float64, std::byte* dest, ir::type::Float, const std::byte* src) const;

    void assign(const ir::type::Reference& dest_type, std::byte* dest,
                const ir::type::Primitive& src_type, const std::byte* src) const;
    void assign(const ir::type::Primitive& dest_type, std::byte* dest,
                const ir::type::Primitive& src_type, const std::byte* src) const;
    void assign(const ir::type::Primitive& dest_type, std::byte* dest,
                const ir::type::Reference& src_type, const std::byte* src) const;
    void assign(const ir::type::Sum& dest_type, std::byte* dest, const ir::Type& src_type,
                const std::byte* src) const;
    void assign(const ir::type::Array& dest_type, std::byte* dest, const ir::type::Array& src_type,
                const std::byte* src) const;
    void assign(const ir::type::Reference& dest_type, std::byte* dest,
                const ir::type::Reference& src_type, const std::byte* src) const;
    void assign(const ir::type::Reference& dest_type, std::byte* dest,
                const ir::type::Array& src_type, const std::byte* src) const;
    void assign(const ir::type::Product& dest_type, std::byte* dest,
                const ir::type::Product& src_type, const std::byte* src) const;
    void assign(const Type& dest_type, std::byte* dest, const Type& src_type,
                const std::byte* src) const;

    void assign(View& dest, const View& src) const;

    void execute(const BinaryInst& inst, const View& lhs, const View& rhs, View& ret);
    void execute(const UnaryInst& inst, const View& operand, View& ret);
    void execute(const CallInst& inst, const std::vector<View>& srcs, View& ret);

    auto execute(Block& block, Block* prev, StackFrame& frame, View& ret) -> Block*;
    void execute(const Func& func, const std::vector<View>& args, View& ret);
    void execute(const vm::BuiltinFunc& func, const std::vector<View>& args, View& ret);

    [[nodiscard]] auto view_of(const LeftValue& lval, const StackFrame& frame) const -> View {
        auto fn = [](const LeftValue& val, const StackFrame& frame) -> std::optional<View> {
            return match(
                val,
                [&](const NamedValue& var) -> std::optional<View> {
                    auto it = frame.vars.find(var.def);
                    if (it == frame.vars.end()) return std::nullopt;
                    auto view = it->second;
                    // use runtime dynamic type
                    // (NamedValue can be assigned multiple times with different types
                    // (for simulating polymorphism of registers in ir::lowering))
                    if (var.type.is<type::Reference>()) {
                        view.type = var.type;
                    }
                    return view;
                },
                [&](const TempValue& temp) -> std::optional<View> {
                    if (temp.id < frame.temps.size()) return frame.temps[temp.id];
                    return std::nullopt;
                },
                [&](const SSAValue& ssa) -> std::optional<View> {
                    auto it = frame.vars.find(ssa.def);
                    if (it != frame.vars.end()) return it->second;
                    return std::nullopt;
                });
        };
        auto local = fn(lval, frame);
        if (local) return *local;
        auto global = fn(lval, global_frame);
        if (global) return *global;
        throw COMPILER_ERROR(fmt::format("Undefined variable: {}", lval));
    }

    [[nodiscard]] auto view_of(const ConstexprValue& c) const -> View {
        return match(
            c.val,
            [&](const std::unique_ptr<std::byte[]>& vec) -> View {
                return View{.data = vec.get(), .type = c.type};
            },
            [&](const auto& val) -> View {
                return View{.data = (std::byte*)&val, .type = c.type};
            });
    }

    [[nodiscard]] auto view_of(const Value& value, const StackFrame& frame) const -> View {
        return match(
            value, [&](const auto& lval) { return view_of(lval, frame); },
            [&](const ConstexprValue& c) { return view_of(c); });
    }

    [[nodiscard]] auto view_of(const std::optional<LeftValue>& lval, const StackFrame& frame) const
        -> View {
        if (lval) return view_of(*lval, frame);
        return View{.data = nullptr, .type = type::construct<void>()};
    }

    void alloc(StackFrame& frame, Alloc* alloc, std::byte* buffer) const;
    [[nodiscard]] size_t stackSize(const std::unique_ptr<Alloc>& alloc) const;
    [[nodiscard]] size_t stackSize(const Type& type) const;

    using BinaryOpFunc = std::function<void(View& dest, const View& lhs, const View& rhs)>;
    std::unordered_map<InstOp, BinaryOpFunc> binary_ops;

    using UnaryOpFunc = std::function<void(View& dest, const View& operand)>;
    std::unordered_map<UnaryInstOp, UnaryOpFunc> unary_ops;

    const Program* program;

    std::istream& input;
    std::ostream& output;

    StackFrame global_frame;
    std::vector<std::pair<StackFrame*, const Func*>> active_frames;

    // Captured program output/input during debug mode
    std::ostringstream debug_output_buf;
    std::streambuf* saved_output_buf{nullptr};
    std::istringstream debug_input_buf;
    std::streambuf* saved_input_buf{nullptr};

    struct Perf {
        size_t num_insts{0};
    } perf_counter;

    std::ostream* trace_stream;

    struct DebugState {
        std::unordered_set<const void*> breakpoints;
        bool stepping{false};
        std::vector<std::function<bool()>> breakpoint_conditions;

        // execution context — set by exec.cpp before each debug_trigger() call
        const Block* current_block{nullptr};
        const void* current_inst{nullptr};  // &Inst or &Exit (for highlighting)
        size_t selected_frame_idx{0};       // which frame to show vars for; f up/down moves this
    } debug_state;

    void debug_trigger(const void* location = nullptr) {
        if (location && debug_state.breakpoints.count(location)) {
            info(fmt::format("hit breakpoint at {}", fmt::ptr(location)));
            debug_state.stepping = true;
        }
        for (size_t i = 0; i < debug_state.breakpoint_conditions.size(); i++) {
            auto& cond = debug_state.breakpoint_conditions[i];
            if (cond()) {
                info(fmt::format("hit conditional breakpoint #{}", i));
                debug_state.stepping = true;
                break;
            }
        }
        if (debug_state.stepping) {
            debug_tui();
        }
    }

    void debug_tui();

public:
    uint8_t execute(const Program& program, std::ostream* trace_stream = nullptr,
                    bool debug = false);

    [[nodiscard]] auto perf() const {
        return perf_counter;
    }

    VirtualMachine(std::istream& input, std::ostream& output) : input(input), output(output) {
        using namespace type;
        unary_ops[UnaryInstOp::MOV] =
            unary_ops[UnaryInstOp::CONVERT] = [&](View& dest, const View& operand) {
                assign(dest.type, dest.data, operand.type, operand.data);
            };
        unary_ops[UnaryInstOp::NOT] = [this](View& dest, const View& operand) {
            eval_unary<std::logical_not>(dest, operand);
        };
        unary_ops[UnaryInstOp::NEG] = [this](View& dest, const View& operand) {
            eval_unary<std::negate>(dest, operand);
        };
        unary_ops[UnaryInstOp::BORROW] = [this](View& dest, const View& operand) {
            auto addr = operand.data;
            assign(dest.type, dest.data, Reference::pointer(operand.type, false),
                   (std::byte*)&addr);
        };
        unary_ops[UnaryInstOp::BORROW_MUT] = [this](View& dest, const View& operand) {
            auto addr = operand.data;
            assign(dest.type, dest.data, Reference::pointer(operand.type, true), (std::byte*)&addr);
        };

        auto check_ref = [](const TypeBox& type, const char* op_name) {
            if (!type.is<Reference>()) {
                throw COMPILER_ERROR(
                    fmt::format("{} expected reference type, but got {}", op_name, type));
            }
        };

        unary_ops[UnaryInstOp::LOAD] = [this, check_ref](View& dest, const View& operand) {
            check_ref(operand.type, "load");
            auto ref_type = operand.type.as<Reference>();
            auto addr = *(std::byte**)operand.data;
            assign(dest.type, dest.data, ref_type.elem, addr);
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

        auto check_indexing = [](const TypeBox& base_type, const TypeBox& offset_type) {
            if (!offset_type.is<Primitive>() ||
                !std::holds_alternative<Int32>(offset_type.as<Primitive>())) {
                throw COMPILER_ERROR(
                    fmt::format("Offset must be an integer, but got {}", offset_type));
            }
            if (!base_type.is<Reference>() && !base_type.is<Array>()) {
                throw COMPILER_ERROR(
                    fmt::format("Expected slice reference or array type, but got {}", base_type));
            }
        };

        binary_ops[InstOp::LOAD_ELEM] =
            [this, check_ref, check_indexing](View& dest, const View& lhs, const View& rhs) {
                // NOTE: lhs: pointer, rhs: offset
                check_indexing(lhs.type, rhs.type);
                auto is_ref = lhs.type.is<Reference>();
                if (is_ref) check_ref(lhs.type, "binary load");
                auto offset = *(int*)rhs.data;
                auto elem_type = is_ref ? lhs.type.as<Reference>().elem : lhs.type.as<Array>().elem;
                auto base = is_ref ? *(std::byte**)lhs.data : lhs.data;
                assign(dest.type, dest.data, elem_type, base + offset * size_of(elem_type));
            };

        auto borrow_elem = [this, check_ref, check_indexing](View& dest, const View& lhs,
                                                             const View& rhs, bool readonly) {
            check_indexing(lhs.type, rhs.type);
            auto is_ref = lhs.type.is<Reference>();
            if (is_ref) check_ref(lhs.type, "binary borrow_elem");
            auto offset = *(int*)rhs.data;
            auto elem_type = is_ref ? lhs.type.as<Reference>().elem : lhs.type.as<Array>().elem;
            auto base = is_ref ? *(std::byte**)lhs.data : lhs.data;
            auto elem_ptr = base + offset * size_of(elem_type);
            assign(dest.type, dest.data, elem_type.borrow(readonly), (std::byte*)&elem_ptr);
        };

        binary_ops[InstOp::BORROW_ELEM] = [borrow_elem](View& dest, const View& lhs,
                                                        const View& rhs) {
            borrow_elem(dest, lhs, rhs, true);
        };

        binary_ops[InstOp::BORROW_ELEM_MUT] = [borrow_elem](View& dest, const View& lhs,
                                                            const View& rhs) {
            borrow_elem(dest, lhs, rhs, false);
        };

        binary_ops[InstOp::SHL] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_binary<shift_left>(dest, lhs, rhs);
        };
        binary_ops[InstOp::SHRL] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_binary<shift_right_logical>(dest, lhs, rhs);
        };
        binary_ops[InstOp::SHRA] = [this](View& dest, const View& lhs, const View& rhs) {
            eval_binary<shift_right_arithmetic>(dest, lhs, rhs);
        };

        binary_ops[InstOp::STORE] = [this, check_ref](View& dest, const View& lhs,
                                                      const View& rhs) {
            auto addr = *(std::byte**)lhs.data;
            if ((size_t)addr < 0x1000) {
                throw COMPILER_ERROR(
                    fmt::format("invalid memory access at address {}", fmt::ptr(addr)));
            }
            check_ref(lhs.type, "store");
            auto ref_type = lhs.type.as<Reference>();
            auto elem_type = ref_type.elem;
            if (ref_type.is_slice) {
                auto rhs_type = rhs.type.as<Array>();
                assign(type::Array(elem_type, rhs_type.size), addr, rhs_type, rhs.data);
            } else {
                assign(elem_type, addr, rhs.type, rhs.data);
            }
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

inline const std::unordered_map<std::string, BuiltinFunc> BUILTIN_FUNCS = {
    {
        "memset",
        BuiltinFunc([](View& ret, const std::vector<View>& args, std::istream& input,
                       std::ostream& output) {
            void* dest = *(void**)args[0].data;
            uint8_t value = Match{args[1].type.as<ir::type::Primitive>()}([&](auto type) {
                using T = typename decltype(type)::type;
                return (uint8_t)(*(T*)args[1].data);
            });
            size_t n = Match{args[2].type.as<ir::type::Primitive>()}([&](auto type) {
                using T = typename decltype(type)::type;
                return (size_t)(*(T*)args[2].data);
            });
            std::memset(dest, value, n);
        }),
    },
    {
        "memcpy",
        BuiltinFunc([](View& ret, const std::vector<View>& args, std::istream& input,
                       std::ostream& output) {
            void* dest = *(void**)args[0].data;
            const void* src = *(const void**)args[1].data;
            size_t n = Match{args[2].type.as<ir::type::Primitive>()}([&](auto type) {
                using T = typename decltype(type)::type;
                return (size_t)(*(T*)args[2].data);
            });
            std::memcpy(dest, src, n);
        }),
    },
    {"get_int", BuiltinFunc{[](View& ret, const std::vector<View>& args, std::istream& input,
                               std::ostream& output) {
         int value;
         input >> value;
         Match{ret.type.as<ir::type::Primitive>()}([&](auto type) {
             using T = typename decltype(type)::type;
             *(T*)ret.data = (T)value;
         });
     }}},
    {"get_float", BuiltinFunc{[](View& ret, const std::vector<View>& args, std::istream& input,
                                 std::ostream& output) {
         std::string str;
         input >> str;
         float value;
         if (str.back() == 'f' || str.back() == 'F') {
             value = std::stof(str.substr(0, str.size() - 1));
         } else {
             value = std::stof(str);
         }
         Match{ret.type.as<ir::type::Primitive>()}([&](auto type) {
             using T = typename decltype(type)::type;
             *(T*)ret.data = (T)value;
         });
     }}},
    {"get_double", BuiltinFunc{[](View& ret, const std::vector<View>& args, std::istream& input,
                                  std::ostream& output) {
         double value;
         input >> value;
         Match{ret.type.as<ir::type::Primitive>()}([&](auto type) {
             using T = typename decltype(type)::type;
             *(T*)ret.data = (T)value;
         });
     }}},
    {"print_int", BuiltinFunc{[](View& ret, const std::vector<View>& args, std::istream& input,
                                 std::ostream& output) {
         int value = Match{args[0].type.as<ir::type::Primitive>()}([&](auto type) {
             using T = typename decltype(type)::type;
             return (int)(*(T*)args[0].data);
         });
         output << value << '\n';
     }}},
    {"print_float", BuiltinFunc{[](View& ret, const std::vector<View>& args, std::istream& input,
                                   std::ostream& output) {
         float value = Match{args[0].type.as<ir::type::Primitive>()}([&](auto type) {
             using T = typename decltype(type)::type;
             return (float)(*(T*)args[0].data);
         });
         output << fmt::format("{:.6f}\n", value);
     }}},
    {"print_double", BuiltinFunc{[](View& ret, const std::vector<View>& args, std::istream& input,
                                    std::ostream& output) {
         double value = Match{args[0].type.as<ir::type::Primitive>()}([&](auto type) {
             using T = typename decltype(type)::type;
             return (double)(*(T*)args[0].data);
         });
         output << fmt::format("{:.6f}\n", value);
     }}},
    {"print_bool", BuiltinFunc{[](View& ret, const std::vector<View>& args, std::istream& input,
                                  std::ostream& output) {
         bool value = Match{args[0].type.as<ir::type::Primitive>()}([&](auto type) {
             using T = typename decltype(type)::type;
             return (bool)(*(T*)args[0].data);
         });
         output << (value ? "true" : "false") << '\n';
     }}},
};

}  // namespace ir::vm