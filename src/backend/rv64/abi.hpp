#pragma once

#include "backend/ir/lowering/abi.hpp"

namespace rv64 {

namespace {
inline const ir::lowering::RegisterABI GPR = {
    .size = 32,
    .caller_saved = {1, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 28, 29, 30, 31},
    .callee_saved = {2, 8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27},
    .reserved = {0, 3, 4},
    .parameters = {10, 11, 12, 13, 14, 15, 16, 17},
    .return_value = 10,
};

inline const ir::lowering::RegisterABI FPR = {
    .size = 32,
    .caller_saved = {0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 28, 29, 30, 31},
    .callee_saved = {8, 9, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27},
    .parameters = {10, 11, 12, 13, 14, 15, 16, 17},
    .return_value = 10,
};
inline size_t size_of(const ir::Type& type) {
    using namespace ir::type;
    if (type.is<Primitive>()) {
        Match{type.as<Primitive>()}([](const Int&) { return 4; }, [](const Bool&) { return 1; },
                                    [](const Float&) { return 4; },
                                    [](const Double&) { return 8; });
    }
    throw UNIMPLEMENTED_ERROR("size_of: non-primitive type");
}
}  // namespace

inline const ir::lowering::TargetABI ABI = {
    .reg = {.generals = GPR, .floats = FPR},
    .mem = {.stack_alignment = 16, .size = size_of, .align = size_of},
};

}  // namespace rv64
