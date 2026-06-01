#pragma once

#include "backend/ir/lowering/abi.hpp"
#include "inst.hpp"

#include <string>

namespace rv64 {

namespace abi {

inline std::string general_reg_name(size_t index) {
    return fmt::format("{}", GeneralReg(index));
}
inline std::string floating_reg_name(size_t index) {
    return fmt::format("{}", FloatReg(index));
}

inline const ir::lowering::RegisterABI GPR = {
    .size = 32,
    .caller_saved = {5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 28, 29, 30, 31},
    .callee_saved = {8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27},
    .reserved = {0, 1, 2, 3, 4},
    .parameters = {10, 11, 12, 13, 14, 15, 16, 17},
    .return_value = 10,
    .name = general_reg_name,
};

inline const ir::lowering::RegisterABI FPR = {
    .size = 32,
    .caller_saved = {0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 28, 29, 30, 31},
    .callee_saved = {8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27},
    .parameters = {10, 11, 12, 13, 14, 15, 16, 17},
    .return_value = 10,
    .name = floating_reg_name,
};

inline size_t size_of(const ir::Type& type) {
    using namespace ir::type;
    if (type.is<Primitive>()) {
        return Match{type.as<Primitive>()}(
            [](const Int&) { return 8; }, [](const Float&) { return 8; },
            [](const Int32&) { return 4; }, [](const Int1&) { return 1; },
            [](const Float32&) { return 4; }, [](const Float64&) { return 8; });
    }
    if (type == ir::type::unit()) {
        return 0;
    }
    throw UNIMPLEMENTED_ERROR("size_of: non-primitive type");
}

}  // namespace abi

inline const ir::lowering::TargetABI ABI = {
    .reg = {.generals = abi::GPR, .floats = abi::FPR},
    .mem = {.stack_alignment = 16, .size = abi::size_of, .align = abi::size_of},
};

}  // namespace rv64
