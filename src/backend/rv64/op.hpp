#pragma once
#define FMT_HEADER_ONLY
#include "fmt/format.h"
#include "utils/diagnosis.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace rv64 {

struct GeneralReg {
    uint8_t id = 0;  // x0 - x31
    GeneralReg() = default;
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
    uint8_t id = 0;  // f0 - f31
    [[nodiscard]] auto toString() const -> std::string {
        static std::array<const char*, 32> names = {
            "ft0", "ft1", "ft2", "ft3", "ft4",  "ft5",  "ft6", "ft7", "fs0",  "fs1", "fa0",
            "fa1", "fa2", "fa3", "fa4", "fa5",  "fa6",  "fa7", "fs2", "fs3",  "fs4", "fs5",
            "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};
        return names[id];
    }
    FloatReg() = default;
    FloatReg(uint8_t id) : id(id) {
        if (id >= 32) {
            throw COMPILER_ERROR(fmt::format("Invalid float register f{}", id));
        }
    }
};

enum class OpR : uint8_t {
    ADD,
    SUB,
    SLL,
    SLT,
    SLTU,
    XOR,
    SRL,
    SRA,
    OR,
    AND,
    MUL,
    DIV,
    REM,
    ADDW,
    SUBW,
    SLLW,
    SRLW,
    SRAW,
    MULW,
    DIVW,
    REMW,
};

enum class OpFR : uint8_t {
    FADD_S,
    FADD_D,
    FSUB_S,
    FSUB_D,
    FMUL_S,
    FMUL_D,
    FDIV_S,
    FDIV_D,
    FSQRT_S,
    FSQRT_D,
    FSGNJ_S,
    FSGNJ_D,
    FSGNJN_S,
    FSGNJN_D,
    FEQ_S,
    FEQ_D,
    FLT_S,
    FLT_D,
    FLE_S,
    FLE_D,
    FCVT_W_S,
    FCVT_W_D,
    FCVT_L_S,
    FCVT_L_D,
    FCVT_S_W,
    FCVT_D_W,
    FCVT_S_L,
    FCVT_D_L,
    FCVT_S_D,
    FCVT_D_S,
    FMV_X_W,
    FMV_W_X,
};

enum class OpI : uint8_t {
    ADDI,
    SLLI,
    SLTI,
    SLTIU,
    XORI,
    SRLI,
    SRAI,
    ORI,
    ANDI,

    LD,
    LW,
    LWU,
    LB,
    LBU,
    LH,
    LHU,

    JALR,

    SD,
    SW,
    SB,
    SH,

    ADDIW,
    SLLIW,
    SRLIW,
    SRAIW,
};

enum class OpB : uint8_t {
    BEQ,
    BNE,
    BLT,
    BGE,
    BLTU,
    BGEU,
};

enum class OpFI : uint8_t {
    FLW,
    FLD,
    FSW,
    FSD,
};

enum class OpJ : uint8_t {
    JAL,
};

enum class OpU : uint8_t {
    LUI,
    AUIPC,
};

// opcode name strings
inline std::string toString(OpR op) {
    switch (op) {
        case OpR::ADD: return "add";
        case OpR::SUB: return "sub";
        case OpR::SLL: return "sll";
        case OpR::SLT: return "slt";
        case OpR::SLTU: return "sltu";
        case OpR::XOR: return "xor";
        case OpR::SRL: return "srl";
        case OpR::SRA: return "sra";
        case OpR::OR: return "or";
        case OpR::AND: return "and";
        case OpR::MUL: return "mul";
        case OpR::DIV: return "div";
        case OpR::REM: return "rem";
        case OpR::ADDW: return "addw";
        case OpR::SUBW: return "subw";
        case OpR::SLLW: return "sllw";
        case OpR::SRLW: return "srlw";
        case OpR::SRAW: return "sraw";
        case OpR::MULW: return "mulw";
        case OpR::DIVW: return "divw";
        case OpR::REMW: return "remw";
    }
    return "?";
}

inline std::string toString(OpFR op) {
    switch (op) {
        case OpFR::FADD_S: return "fadd.s";
        case OpFR::FADD_D: return "fadd.d";
        case OpFR::FSUB_S: return "fsub.s";
        case OpFR::FSUB_D: return "fsub.d";
        case OpFR::FMUL_S: return "fmul.s";
        case OpFR::FMUL_D: return "fmul.d";
        case OpFR::FDIV_S: return "fdiv.s";
        case OpFR::FDIV_D: return "fdiv.d";
        case OpFR::FSQRT_S: return "fsqrt.s";
        case OpFR::FSQRT_D: return "fsqrt.d";
        case OpFR::FSGNJ_S: return "fsgnj.s";
        case OpFR::FSGNJ_D: return "fsgnj.d";
        case OpFR::FSGNJN_S: return "fsgnjn.s";
        case OpFR::FSGNJN_D: return "fsgnjn.d";
        case OpFR::FEQ_S: return "feq.s";
        case OpFR::FEQ_D: return "feq.d";
        case OpFR::FLT_S: return "flt.s";
        case OpFR::FLT_D: return "flt.d";
        case OpFR::FLE_S: return "fle.s";
        case OpFR::FLE_D: return "fle.d";
        case OpFR::FCVT_W_S: return "fcvt.w.s";
        case OpFR::FCVT_W_D: return "fcvt.w.d";
        case OpFR::FCVT_L_S: return "fcvt.l.s";
        case OpFR::FCVT_L_D: return "fcvt.l.d";
        case OpFR::FCVT_S_W: return "fcvt.s.w";
        case OpFR::FCVT_D_W: return "fcvt.d.w";
        case OpFR::FCVT_S_L: return "fcvt.s.l";
        case OpFR::FCVT_D_L: return "fcvt.d.l";
        case OpFR::FCVT_S_D: return "fcvt.s.d";
        case OpFR::FCVT_D_S: return "fcvt.d.s";
        case OpFR::FMV_X_W: return "fmv.x.w";
        case OpFR::FMV_W_X: return "fmv.w.x";
    }
    return "?";
}

inline std::string toString(OpI op) {
    switch (op) {
        case OpI::ADDI: return "addi";
        case OpI::SLLI: return "slli";
        case OpI::SLTI: return "slti";
        case OpI::SLTIU: return "sltiu";
        case OpI::XORI: return "xori";
        case OpI::SRLI: return "srli";
        case OpI::SRAI: return "srai";
        case OpI::ORI: return "ori";
        case OpI::ANDI: return "andi";
        case OpI::LD: return "ld";
        case OpI::LW: return "lw";
        case OpI::LWU: return "lwu";
        case OpI::LB: return "lb";
        case OpI::LBU: return "lbu";
        case OpI::LH: return "lh";
        case OpI::LHU: return "lhu";
        case OpI::JALR: return "jalr";
        case OpI::SD: return "sd";
        case OpI::SW: return "sw";
        case OpI::SB: return "sb";
        case OpI::SH: return "sh";
        case OpI::ADDIW: return "addiw";
        case OpI::SLLIW: return "slliw";
        case OpI::SRLIW: return "srliw";
        case OpI::SRAIW: return "sraiw";
    }
    return "?";
}

inline std::string toString(OpB op) {
    switch (op) {
        case OpB::BEQ: return "beq";
        case OpB::BNE: return "bne";
        case OpB::BLT: return "blt";
        case OpB::BGE: return "bge";
        case OpB::BLTU: return "bltu";
        case OpB::BGEU: return "bgeu";
    }
    return "?";
}

inline std::string toString(OpFI op) {
    switch (op) {
        case OpFI::FLW: return "flw";
        case OpFI::FLD: return "fld";
        case OpFI::FSW: return "fsw";
        case OpFI::FSD: return "fsd";
    }
    return "?";
}

inline std::string toString(OpJ op) {
    switch (op) {
        case OpJ::JAL: return "jal";
    }
    return "?";
}

inline std::string toString(OpU op) {
    switch (op) {
        case OpU::LUI: return "lui";
        case OpU::AUIPC: return "auipc";
    }
    return "?";
}

}  // namespace rv64
