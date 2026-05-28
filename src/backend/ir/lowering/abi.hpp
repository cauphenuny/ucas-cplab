#pragma once

#include "backend/ir/ir.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <set>
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

std::vector<std::optional<size_t>>
assign_param_regs(const std::vector<std::unique_ptr<Alloc>>& params, TargetABI& abi) {
    using namespace type;
    size_t num_general = 0, num_float = 0;
    std::vector<std::optional<size_t>> param_regs;
    param_regs.reserve(params.size());
    auto general = [&] {
        if (num_general < abi.reg.generals.parameters.size()) {
            param_regs.emplace_back(abi.reg.generals.parameters[num_general]);
            num_general++;
        }
    };
    auto floating = [&] {
        if (num_float < abi.reg.floats.parameters.size()) {
            param_regs.emplace_back(abi.reg.floats.parameters[num_float]);
            num_float++;
        }
    };
    for (const auto& param : params) {
        if (!param->type.is<Primitive>()) throw UNIMPLEMENTED_ERROR("non-primitive parameter type");
        Match{param->type.as<Primitive>()}(
            [&](const Int&) { general(); }, [&](const Bool& b) { general(); },
            [&](const Float& f) { floating(); }, [&](const Double& d) { floating(); });
    }
    return param_regs;
}

}  // namespace ir::lowering
