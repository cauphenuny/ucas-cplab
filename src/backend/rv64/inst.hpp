#pragma once
#define FMT_HEADER_ONLY
#include "fmt/format.h"
#include "utils/diagnosis.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <variant>

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
        static std::array<const char*, 32> names = {
            "ft0", "ft1", "ft2", "ft3", "ft4",  "ft5",  "ft6", "ft7", "fs0",  "fs1", "fa0",
            "fa1", "fa2", "fa3", "fa4", "fa5",  "fa6",  "fa7", "fs2", "fs3",  "fs4", "fs5",
            "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};
        return names[id];
    }
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

    BEQ,
    BNE,
    BLT,
    BGE,
    BLTU,
    BGEU,

    ADDIW,
    SLLIW,
    SRLIW,
    SRAIW,
    ADDW,
    SUBW,
    SLLW,
    SRLW,
    SRAW,
    MULW,
    DIVW,
    REMW,
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

enum class Pseudo : uint8_t {
    LI,
    MV,
    NOT,
    NEG,
    NEGW,
    SEQZ,
    SNEZ,
    BEQZ,
    BNEZ,
    J,
    CALL,
    RET,
    LA,
    LOAD_GLOBAL,
    STORE_GLOBAL,
};

struct Module;

struct InstR {
    OpR op;
    GeneralReg rd, rs1, rs2;
};

struct InstFR {
    OpFR op;
    FloatReg rd, rs1, rs2;
};

struct InstI {
    OpI op;
    GeneralReg rd, rs1;
    int32_t imm;
};

struct InstFI {
    OpFI op;
    FloatReg rd, rs1;
    int32_t imm;
};

struct InstJ {
    OpJ op;
    GeneralReg rd;
    std::string target;
};

struct InstU {
    OpU op;
    GeneralReg rd;
    int64_t imm;
};

struct PseudoInst {
    Pseudo op;
    GeneralReg rd;
    GeneralReg rs1;
    int64_t imm;
    std::string target;
    std::string symbol;
    FloatReg frd, frs1;
    size_t elem_size{4};
};

using Inst = std::variant<InstR, InstFR, InstI, InstFI, InstJ, InstU, PseudoInst>;

}  // namespace rv64
