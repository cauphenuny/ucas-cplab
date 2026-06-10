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
};

struct PseudoLI {
    GeneralReg rd;
    int64_t imm;
};

struct PseudoB {
    enum Op : uint8_t {
        BEQZ,
        BNEZ,
    } op;
    GeneralReg rs1;
    std::string target;
};

struct PseudoJ {
    enum Op : uint8_t {
        J,
        CALL,
    } op;
    std::string target;
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
};

struct PseudoRet {};

using Inst = std::variant<InstR, InstFR, InstI, InstFI, InstJ, InstU, PseudoR, PseudoLI, PseudoL,
                          PseudoB, PseudoJ, PseudoRet>;

struct Global {
    std::string name;
    ir::Type type;
    std::optional<ir::ConstexprValue> init;
    bool comptime{false};
    bool is_zero_init() const {
        if (!init) return true;
        bool zero = false;
        Match{init->val}([&](std::monostate) { zero = true; }, [&](int v) { zero = (v == 0); },
                         [&](int64_t v) { zero = (v == 0); }, [&](float v) { zero = (v == 0.0f); },
                         [&](double v) { zero = (v == 0.0); }, [&](bool v) { zero = !v; },
                         [&](const std::unique_ptr<std::byte[]>&) { zero = false; });
        return zero;
    }
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
};

struct AsmFunc {
    std::string name;
    FrameLayout frame;
    std::vector<AsmBlock> blocks;
};

struct FloatLiteral {
    std::string label;
    ir::ConstexprValue value;
};

struct Module {
    std::vector<Global> globals;
    std::vector<AsmFunc> funcs;
    std::vector<FloatLiteral> float_literals;
};

}  // namespace rv64
