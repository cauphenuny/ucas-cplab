#pragma once
#define FMT_HEADER_ONLY
#include "fmt/format.h"
#include "utils/diagnosis.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace rv64 {

struct GeneralReg {
    uint8_t id;  // x0 - x31
    GeneralReg(uint8_t id) : id(id) {
        if (id >= 32) {
            throw COMPILER_ERROR(fmt::format("Invalid general register x{}", id));
        }
    }
    [[nodiscard]] auto toString() const -> std::string {
        static std::array<const char*, 32> names = {
            "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
            "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
            "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
        return names[id];
    }
};

struct FloatReg {
    uint8_t id;  // f0 - f31
    [[nodiscard]] auto toString() const -> std::string {
        static std::array<const char*, 32> names = {"ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
                                                 "fs0", "fs1", "fa0", "fa1", "fa2", "fa3", "fa4", "fa5",
                                                 "fa6", "fa7", "fs2", "fs3", "fs4", "fs5", "fs6",
                                                 "fs7", "fs8", "fs9", "fs10", "fs11", "ft8",
                                                 "ft9", "ft10", "ft11"};
        return names[id];
    }
    FloatReg(uint8_t id) : id(id) {
        if (id >= 32) {
            throw COMPILER_ERROR(fmt::format("Invalid float register f{}", id));
        }
    }
};

}  // namespace rv64