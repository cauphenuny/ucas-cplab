#pragma once

#include "backend/ir/lowering/abi.hpp"
#include "op.hpp"

namespace rv64 {

namespace abi {

inline const ir::lowering::RegisterABI GPR = {
    .size = 32,
    .caller_saved = {6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 28, 29, 30, 31},
    .callee_saved = {1, 8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27},
    .reserved = {0, 2, 3, 4, 5},  // x5(t0) is reserved for instruction selection
    .parameters = {10, 11, 12, 13, 14, 15, 16, 17},
    .return_value = 10,
    .name = [](size_t index) { return fmt::format("{}", GeneralReg(index)); },
};

inline const ir::lowering::RegisterABI FPR = {
    .size = 32,
    .caller_saved = {0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 28, 29, 30, 31},
    .callee_saved = {8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27},
    .reserved = {5},  // f5(ft0) is reserved for instruction selection
    .parameters = {10, 11, 12, 13, 14, 15, 16, 17},
    .return_value = 10,
    .name = [](size_t index) { return fmt::format("{}", FloatReg(index)); },
};

inline size_t size(const ir::Type& type) {
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
    if (type.is<Reference>()) {
        return 8;
    }
    if (type.is<Array>()) {
        auto& array_type = type.as<Array>();
        return size(array_type.elem) * array_type.size;
    }
    throw UNIMPLEMENTED_ERROR(fmt::format("size: non-trivial type {}", type));
}

}  // namespace abi

inline const ir::lowering::TargetABI ABI = {
    .reg = {.generals = abi::GPR, .floats = abi::FPR, .return_addr = 1},
    .mem = {.stack_alignment = 16, .size = abi::size, .align = abi::size},
};

}  // namespace rv64
