#pragma once

#include "backend/ir/ir.h"
#include "op.hpp"
#include "utils/match.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace rv64 {

struct Module;

struct InstR {
    OpR op;
    GeneralReg rd, rs1, rs2;
    [[nodiscard]] std::string toString() const;
};

struct InstFR {
    OpFR op;
    std::variant<FloatReg, GeneralReg> rd, rs1;
    std::variant<std::monostate, FloatReg, GeneralReg> rs2;
    [[nodiscard]] std::string toString() const;
};

struct InstI {
    OpI op;
    GeneralReg rd, rs1;
    int32_t imm;
    [[nodiscard]] std::string toString() const;
};

struct InstFI {
    OpFI op;
    FloatReg rd;
    GeneralReg rs1;
    int32_t imm;
    [[nodiscard]] std::string toString() const;
};

struct InstJ {
    OpJ op;
    GeneralReg rd;
    std::string target;
    [[nodiscard]] std::string toString() const;
};

struct InstU {
    OpU op;
    GeneralReg rd;
    int64_t imm;
    [[nodiscard]] std::string toString() const;
};

struct PseudoR {
    enum Op : uint8_t {
        MV,
        NOT,
        NEG,
        NEGW,
        SEQZ,
        SNEZ,
    } op;
    GeneralReg rd, rs1;
    [[nodiscard]] std::string toString() const;
};

struct PseudoLI {
    GeneralReg rd;
    int64_t imm;
    [[nodiscard]] std::string toString() const;
};

struct PseudoB {
    enum Op : uint8_t {
        BEQZ,
        BNEZ,
    } op;
    GeneralReg rs1;
    std::string target;
    [[nodiscard]] std::string toString() const;
};

struct PseudoJ {
    enum Op : uint8_t {
        J,
        CALL,
    } op;
    std::string target;
    [[nodiscard]] std::string toString() const;
};

struct PseudoL {
    enum Op : uint8_t {
        LA,   // load address
        LGD,  // load global, double
        LGW,  // load global, word
        SGD,  // store global, double
        SGW,  // store global, word
    } op;
    GeneralReg rd;
    std::string symbol;
    [[nodiscard]] std::string toString() const;
};

struct PseudoRet {
    [[nodiscard]] std::string toString() const;
};

using Inst = std::variant<InstR, InstFR, InstI, InstFI, InstJ, InstU, PseudoR, PseudoLI, PseudoL,
                          PseudoB, PseudoJ, PseudoRet>;

struct Global {
    std::string name;
    ir::Type type;
    std::optional<ir::ConstexprValue> init;
    bool comptime{false};
    [[nodiscard]] bool is_zero_init() const {
        if (!init) return true;
        return *init == ir::ConstexprValue::zeros_like(type);
    }
    [[nodiscard]] std::string toString() const;
};

struct FrameLayout {
    size_t total_size{0};
    size_t ra_offset{0};
    std::vector<std::pair<GeneralReg, size_t>> saved_gprs;
    std::vector<std::pair<FloatReg, size_t>> saved_fprs;
    std::unordered_map<const ir::Alloc*, size_t> spill_offsets;

    [[nodiscard]] auto offset_of(const ir::Alloc* alloc) const -> size_t {
        return spill_offsets.at(alloc);
    }
    [[nodiscard]] auto has_spill(const ir::Alloc* alloc) const -> bool {
        return spill_offsets.count(alloc) > 0;
    }
};

struct AsmBlock {
    std::string label;
    std::vector<Inst> insts;
    [[nodiscard]] std::string toString() const;
};

struct AsmFunc {
    std::string name;
    FrameLayout frame;
    std::vector<AsmBlock> blocks;
    [[nodiscard]] std::string toString() const;
};

struct FloatLiteral {
    std::string label;
    ir::ConstexprValue value;
    [[nodiscard]] std::string toString() const;
};

struct Module {
    std::vector<Global> globals;
    std::vector<AsmFunc> funcs;
    std::vector<FloatLiteral> float_literals;
    [[nodiscard]] std::string toString() const;
};

}  // namespace rv64
