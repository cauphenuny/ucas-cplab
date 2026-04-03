#pragma once
#define FMT_HEADER_ONLY
#include "fmt/format.h"

#include <array>
#include <cstdint>
#include <string>

namespace rv64 {

enum class Reg : uint8_t {
    ZERO,
    RA,
    SP,
    GP,
    TP,
    T0,
    T1,
    T2,
    S0,  // a.k.a. FP
    S1,
    A0,
    A1,
    A2,
    A3,
    A4,
    A5,
    A6,
    A7,
    S2,
    S3,
    S4,
    S5,
    S6,
    S7,
    S8,
    S9,
    S10,
    S11,
    T3,
    T4,
    T5,
    T6,
};

std::string toString(Reg r) {
    static std::array<const char*, 32> names = {"zero", "ra", "sp",  "gp",  "tp", "t0", "t1", "t2",
                                                "s0",   "s1", "a0",  "a1",  "a2", "a3", "a4", "a5",
                                                "a6",   "a7", "s2",  "s3",  "s4", "s5", "s6", "s7",
                                                "s8",   "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
    return names[static_cast<int>(r)];
}

struct FloatReg {
    uint8_t id;  // f0 - f31
    [[nodiscard]] auto toString() const -> std::string {
        return fmt::format("f{}", id);
    }
};

}  // namespace rv64