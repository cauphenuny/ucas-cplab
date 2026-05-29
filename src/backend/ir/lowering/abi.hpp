#pragma once

#include "backend/ir/ir.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
#include <variant>
#include <vector>

namespace ir::lowering {

struct RegisterABI {
    size_t size;

    std::set<size_t> caller_saved;
    std::set<size_t> callee_saved;
    std::set<size_t> reserved;

    std::vector<size_t> parameters;
    size_t return_value;
};

struct MemoryABI {
    size_t stack_alignment;
    size_t (*size)(const Type& type);
    size_t (*align)(const Type& type);
};

struct TargetABI {
    struct {
        RegisterABI generals;
        RegisterABI floats;
    } reg;

    MemoryABI mem;
};

inline bool is_fp(const Type& type) {
    using namespace type;
    return type.is<Primitive>() && (std::holds_alternative<Float>(type.as<Primitive>()) ||
                                    std::holds_alternative<Double>(type.as<Primitive>()));
}

inline std::vector<std::optional<size_t>> assign_call_regs(const std::vector<Type>& types,
                                                           TargetABI& abi) {
    using namespace type;
    size_t num_general = 0, num_float = 0;
    std::vector<std::optional<size_t>> param_regs;
    auto assign = [&](size_t& counter, const std::vector<size_t>& params) {
        if (counter < params.size()) {
            param_regs.emplace_back(params[counter]);
            counter++;
        } else {
            param_regs.emplace_back(std::nullopt);
        }
    };
    for (const auto& type : types) {
        if (!type.is<Primitive>()) throw UNIMPLEMENTED_ERROR("non-primitive parameter type");
        if (is_fp(type)) {
            assign(num_float, abi.reg.floats.parameters);
        } else {
            assign(num_general, abi.reg.generals.parameters);
        }
    }
    return param_regs;
}

inline std::vector<std::optional<size_t>>
assign_param_regs(const std::vector<std::unique_ptr<Alloc>>& params, TargetABI& abi) {
    std::vector<Type> types;
    types.reserve(params.size());
    for (const auto& param : params) types.push_back(param->type);
    return assign_call_regs(types, abi);
}

inline std::vector<std::optional<size_t>> assign_arg_regs(const std::vector<Value>& args,
                                                          TargetABI& abi) {
    std::vector<Type> types;
    types.reserve(args.size());
    for (const auto& arg : args) types.push_back(type_of(arg));
    return assign_call_regs(types, abi);
}

}  // namespace ir::lowering
