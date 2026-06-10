#pragma once

#include "backend/ir/ir.h"
#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/lowering/regalloc/precolorize.hpp"
#include "backend/ir/type.hpp"
#include "inst.hpp"
#include "utils/match.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace rv64::isel {

inline std::string name_of(const ir::NameDef& def) {
    return Match{def}([&](const auto* p) -> std::string { return p->name; });
}

// Look up a LeftValue in the register ColorMap (returns register ID, or nullopt if ref/const)
inline std::optional<size_t> lookup_reg(const ir::lowering::ColorMap& regs,
                                        const ir::LeftValue& lv) {
    auto it = regs.find(lv);
    if (it != regs.end()) return it->second;
    return std::nullopt;
}

inline std::optional<size_t> lookup_reg(const ir::lowering::ColorMap& regs, const ir::Value& val) {
    if (auto* lv = std::get_if<ir::LeftValue>(&val)) {
        return lookup_reg(regs, *lv);
    }
    return std::nullopt;
}

inline std::optional<int64_t> extract_imm(const ir::ConstexprValue& cv) {
    using namespace ir::type;
    int64_t val = 0;
    bool ok = false;
    Match{cv.val}(
        [&](int v) {
            val = v;
            ok = true;
        },
        [&](int64_t v) {
            val = v;
            ok = true;
        },
        [&](bool v) {
            val = v ? 1 : 0;
            ok = true;
        },
        [&](auto&&) {});
    if (ok) return val;
    return std::nullopt;
}

inline bool is_32bit_op(const ir::Type& t) {
    using namespace ir::type;
    return t.is<Primitive>() && (std::holds_alternative<Int32>(t.as<Primitive>()) ||
                                 std::holds_alternative<Float32>(t.as<Primitive>()));
}

inline bool is_commutative_int(ir::InstOp op) {
    switch (op) {
        case ir::InstOp::ADD:
        case ir::InstOp::MUL:
        case ir::InstOp::AND:
        case ir::InstOp::OR:
        case ir::InstOp::EQ:
        case ir::InstOp::NEQ: return true;
        default: return false;
    }
}

inline int64_t fold_binary_int(ir::InstOp op, int64_t a, int64_t b) {
    switch (op) {
        case ir::InstOp::ADD: return a + b;
        case ir::InstOp::SUB: return a - b;
        case ir::InstOp::MUL: return a * b;
        case ir::InstOp::DIV: return b == 0 ? 0 : a / b;
        case ir::InstOp::MOD: return b == 0 ? 0 : a % b;
        case ir::InstOp::AND: return a & b;
        case ir::InstOp::OR: return a | b;
        case ir::InstOp::LT: return a < b;
        case ir::InstOp::GT: return a > b;
        case ir::InstOp::LEQ: return a <= b;
        case ir::InstOp::GEQ: return a >= b;
        case ir::InstOp::EQ: return a == b;
        case ir::InstOp::NEQ: return a != b;
        default: throw COMPILER_ERROR("cannot fold int");
    }
}

inline bool is_fp_op(const ir::Type& t) {
    using namespace ir::type;
    return t.is<Primitive>() && (std::holds_alternative<Float32>(t.as<Primitive>()) ||
                                 std::holds_alternative<Float64>(t.as<Primitive>()) ||
                                 std::holds_alternative<Float>(t.as<Primitive>()));
}

inline bool is_int_op(const ir::Type& t) {
    using namespace ir::type;
    return t.is<Primitive>() && (std::holds_alternative<Int>(t.as<Primitive>()) ||
                                 std::holds_alternative<Int32>(t.as<Primitive>()) ||
                                 std::holds_alternative<Int1>(t.as<Primitive>()));
}

// Check if NamedValue points to a specific global/alloc (for ref/comptime allocs)
inline const ir::Alloc* resolve_alloc(const ir::Value& val) {
    if (auto* lv = std::get_if<ir::LeftValue>(&val)) {
        if (auto* nv = std::get_if<ir::NamedValue>(&*lv)) {
            if (auto* alloc = std::get_if<const ir::Alloc*>(&nv->def)) {
                return *alloc;
            }
        }
    }
    return nullptr;
}

inline FrameLayout compute_frame(const ir::Func& func, const ir::lowering::TargetABI& abi) {
    FrameLayout layout;
    size_t offset = 0;
    for (auto& local : func.locals()) {
        if (!local->reference) continue;
        // Register-proxy allocs are global, never appear in func.locals()
        size_t sz = abi.mem.size(local->type);
        size_t align = abi.mem.align(local->type);
        if (align > 1) offset = (offset + align - 1) & ~(align - 1);
        layout.spill_offsets[local.get()] = offset;
        offset += sz;
    }
    // align to abi stack alignment
    size_t align = abi.mem.stack_alignment;
    if (align > 1) offset = (offset + align - 1) & ~(align - 1);
    layout.total_size = offset;
    return layout;
}

inline std::string block_label(const std::string& func_name, const std::string& blk_label) {
    return ".L" + func_name + "_" + blk_label;
}

// Forward declares
inline void translate_inst(const ir::Inst& inst, AsmBlock& blk, const FrameLayout& frame,
                           const ir::lowering::TargetABI& abi, const ir::lowering::ColorMap& regs);
inline void translate_exit(const ir::Exit& exit, AsmBlock& blk, const std::string& func_name,
                           const ir::lowering::ColorMap& regs);

inline void translate_unary(const ir::UnaryInst& inst, AsmBlock& blk, const FrameLayout& frame,
                            const ir::lowering::TargetABI& abi,
                            const ir::lowering::ColorMap& regs) {
    using namespace ir::type;

    if (!inst.result) return;  // no destination, skip (shouldn't happen for non-STORE)

    auto rd = lookup_reg(regs, *inst.result);
    if (!rd) return;
    bool fp = is_fp_op(ir::type_of(*inst.result));

    auto gpr = [](size_t id) { return GeneralReg{static_cast<uint8_t>(id)}; };
    auto fpr = [](size_t id) { return FloatReg{static_cast<uint8_t>(id)}; };

    switch (inst.op) {
        case ir::UnaryInstOp::MOV: {
            if (auto* cv = std::get_if<ir::ConstexprValue>(&inst.operand)) {
                if (auto imm = extract_imm(*cv)) {
                    blk.insts.emplace_back(PseudoLI{gpr(*rd), *imm});
                }
            } else if (auto src = lookup_reg(regs, inst.operand)) {
                if (*rd != *src) {
                    if (fp) {
                        bool is_double = !is_32bit_op(ir::type_of(*inst.result));
                        blk.insts.emplace_back(InstFR{is_double ? OpFR::FSGNJ_D : OpFR::FSGNJ_S,
                                                      fpr(*rd), fpr(*src), fpr(*src)});
                    } else {
                        blk.insts.emplace_back(PseudoR{PseudoR::MV, gpr(*rd), gpr(*src)});
                    }
                }
            } else if (auto* alloc = resolve_alloc(inst.operand)) {
                if (!fp && alloc->comptime && alloc->init) {
                    // comptime Alloc: load init value into GPR
                    if (auto imm = extract_imm(*alloc->init)) {
                        blk.insts.emplace_back(PseudoLI{gpr(*rd), *imm});
                    }
                } else if (frame.has_spill(alloc)) {
                    // stack local reference: rd = sp + offset
                    blk.insts.emplace_back(
                        InstI{OpI::ADDI, gpr(*rd), GeneralReg{2}, (int32_t)frame.offset_of(alloc)});
                } else {
                    // global alloc: rd = address of symbol
                    blk.insts.emplace_back(PseudoL{PseudoL::LA, gpr(*rd), alloc->name});
                }
            }
            break;
        }
        case ir::UnaryInstOp::NEG: {
            if (fp) {
                auto src = lookup_reg(regs, inst.operand);
                if (src) {
                    bool is_double = !is_32bit_op(ir::type_of(*inst.result));
                    blk.insts.emplace_back(InstFR{is_double ? OpFR::FSGNJN_D : OpFR::FSGNJN_S,
                                                  fpr(*rd), fpr(*src), fpr(*src)});
                }
            } else {
                auto src = lookup_reg(regs, inst.operand);
                if (src) {
                    if (is_32bit_op(ir::type_of(*inst.result))) {
                        blk.insts.emplace_back(PseudoR{PseudoR::NEGW, gpr(*rd), gpr(*src)});
                    } else {
                        blk.insts.emplace_back(PseudoR{PseudoR::NEG, gpr(*rd), gpr(*src)});
                    }
                } else if (auto* cv = std::get_if<ir::ConstexprValue>(&inst.operand)) {
                    if (auto imm = extract_imm(*cv)) {
                        blk.insts.emplace_back(PseudoLI{gpr(*rd), -*imm});
                    }
                }
            }
            break;
        }
        case ir::UnaryInstOp::NOT: {
            auto src = lookup_reg(regs, inst.operand);
            if (src) {
                auto t = ir::type_of(*inst.result);
                if (t.is<Primitive>() && std::holds_alternative<Int1>(t.as<Primitive>())) {
                    blk.insts.emplace_back(InstI{OpI::XORI, gpr(*rd), gpr(*src), 1});
                } else {
                    blk.insts.emplace_back(PseudoR{PseudoR::SEQZ, gpr(*rd), gpr(*src)});
                }
            } else if (auto* cv = std::get_if<ir::ConstexprValue>(&inst.operand)) {
                if (auto imm = extract_imm(*cv)) {
                    blk.insts.emplace_back(PseudoLI{gpr(*rd), *imm ? 0 : 1});
                }
            }
            break;
        }
        case ir::UnaryInstOp::LOAD: {
            auto* ref_alloc = resolve_alloc(inst.operand);
            // Derive load width from the operand's reference element type,
            // not from the result type (which may be widened after regalloc).
            auto load_elem = type_of(inst.operand)
                    .as<ir::type::Reference>()
                    .elem;
            if (ref_alloc && frame.has_spill(ref_alloc)) {
                size_t off = frame.offset_of(ref_alloc);
                blk.insts.emplace_back(InstI{is_32bit_op(load_elem) ? OpI::LW : OpI::LD, gpr(*rd),
                                             GeneralReg{2}, (int32_t)off});
            } else if (auto base = lookup_reg(regs, inst.operand)) {
                // register dereference — operand has a register, use it as a pointer
                blk.insts.emplace_back(
                    InstI{is_32bit_op(load_elem) ? OpI::LW : OpI::LD, gpr(*rd), gpr(*base), 0});
            } else if (ref_alloc) {
                // symbol without register: load from the symbol address
                auto sz = abi.mem.size(ref_alloc->type);
                PseudoL pi{sz == 8 ? PseudoL::LGD : PseudoL::LGW, gpr(*rd), ref_alloc->name};
                blk.insts.emplace_back(pi);
            }
            break;
        }
        case ir::UnaryInstOp::BORROW:
        case ir::UnaryInstOp::BORROW_MUT:
            throw COMPILER_ERROR("BORROW should be lowered by AddressLowering");
        case ir::UnaryInstOp::CONVERT: {
            // int-to-int conversion — also handles reference→int (taking address of a stack/global
            // alloc that doesn't have a register)
            if (auto src = lookup_reg(regs, inst.operand)) {
                if (is_32bit_op(ir::type_of(inst.operand)) &&
                    !is_32bit_op(ir::type_of(*inst.result))) {
                    blk.insts.emplace_back(InstI{OpI::ADDIW, gpr(*rd), gpr(*src), 0});
                } else {
                    blk.insts.emplace_back(PseudoR{PseudoR::MV, gpr(*rd), gpr(*src)});
                }
            } else if (auto* alloc = resolve_alloc(inst.operand)) {
                if (frame.has_spill(alloc)) {
                    // reference alloc on stack: rd = sp + offset
                    blk.insts.emplace_back(
                        InstI{OpI::ADDI, gpr(*rd), GeneralReg{2}, (int32_t)frame.offset_of(alloc)});
                } else {
                    // global alloc: rd = address of symbol
                    blk.insts.emplace_back(PseudoL{PseudoL::LA, gpr(*rd), alloc->name});
                }
            }
            break;
        }
    }
}

inline auto gpr(size_t id) {
    return GeneralReg{static_cast<uint8_t>(id)};
}
inline auto fpr(size_t id) {
    return FloatReg{static_cast<uint8_t>(id)};
}

inline void translate_binary(const ir::BinaryInst& inst, AsmBlock& blk, const FrameLayout& frame,
                             const ir::lowering::TargetABI& abi,
                             const ir::lowering::ColorMap& regs) {
    using namespace ir::type;

    if (!inst.result) {
        // STORE instruction
        if (inst.op != ir::InstOp::STORE) return;
        auto* ref_alloc = resolve_alloc(inst.lhs);
        auto val = lookup_reg(regs, inst.rhs);

        // Materialize constexpr RHS into t1 (t0 = x5 is reserved for isel, used by PseudoL)
        if (!val) {
            if (auto* cv = std::get_if<ir::ConstexprValue>(&inst.rhs)) {
                if (auto imm = extract_imm(*cv)) {
                    blk.insts.emplace_back(PseudoLI{gpr(5), *imm});  // t0
                    val = 5;
                }
            }
        }
        // Handle byte-buffer literal store (array initializer)
        // Use 4-byte chunks because PseudoLI truncates to int32_t
        if (!val) {
            if (auto* cv = std::get_if<ir::ConstexprValue>(&inst.rhs)) {
                if (auto* buf = std::get_if<std::unique_ptr<std::byte[]>>(&cv->val)) {
                    if (ref_alloc && frame.has_spill(ref_alloc)) {
                        size_t off = frame.offset_of(ref_alloc);
                        size_t sz = abi.mem.size(cv->type);
                        for (size_t i = 0; i < sz; i += 4) {
                            size_t chunk = std::min(sz - i, size_t(4));
                            int32_t v32 = 0;
                            std::memcpy(&v32, buf->get() + i, chunk);
                            OpI op = (chunk == 4) ? OpI::SW : (chunk == 2) ? OpI::SH : OpI::SB;
                            blk.insts.emplace_back(PseudoLI{gpr(5), (int64_t)v32});
                            blk.insts.emplace_back(
                                InstI{op, gpr(5), GeneralReg{2}, (int32_t)(off + i)});
                        }
                        return;
                    }
                }
            }
        }

        if (!val) return;

        // 1) Stack slot (reference with known offset)
        if (ref_alloc && frame.has_spill(ref_alloc)) {
            size_t off = frame.offset_of(ref_alloc);
            blk.insts.emplace_back(InstI{is_32bit_op(ir::type_of(inst.rhs)) ? OpI::SW : OpI::SD,
                                         gpr(*val), GeneralReg{2}, (int32_t)off});
        } else if (auto base = lookup_reg(regs, inst.lhs)) {
            // 2) Register-dereference store: *base = val
            blk.insts.emplace_back(InstI{is_32bit_op(ir::type_of(inst.rhs)) ? OpI::SW : OpI::SD,
                                         gpr(*val), gpr(*base), 0});
        } else if (ref_alloc) {
            // 3) Global symbol store
            auto sz = abi.mem.size(ref_alloc->type);
            if (sz == 8) {
                // la t0, symbol; sd val, 0(t0)
                blk.insts.emplace_back(PseudoL{PseudoL::LA, gpr(5), ref_alloc->name});
                blk.insts.emplace_back(InstI{OpI::SD, gpr(*val), gpr(5), 0});
            } else {
                blk.insts.emplace_back(PseudoL{PseudoL::LA, gpr(5), ref_alloc->name});
                blk.insts.emplace_back(InstI{OpI::SW, gpr(*val), gpr(5), 0});
            }
        }
        return;
    }

    auto rd = lookup_reg(regs, *inst.result);
    if (!rd) return;
    auto t = ir::type_of(*inst.result);

    auto lhs = lookup_reg(regs, inst.lhs);
    auto rhs = lookup_reg(regs, inst.rhs);
    auto rhs_cv = std::get_if<ir::ConstexprValue>(&inst.rhs);

    bool w = is_32bit_op(t);
    bool fp = is_fp_op(t);
    bool is_double = fp && !w;  // Float or Float64 → double precision

    if (fp && lhs && rhs) {
        switch (inst.op) {
            case ir::InstOp::ADD:
                blk.insts.emplace_back(InstFR{is_double ? OpFR::FADD_D : OpFR::FADD_S, fpr(*rd),
                                              fpr(*lhs), fpr(*rhs)});
                break;
            case ir::InstOp::SUB:
                blk.insts.emplace_back(InstFR{is_double ? OpFR::FSUB_D : OpFR::FSUB_S, fpr(*rd),
                                              fpr(*lhs), fpr(*rhs)});
                break;
            case ir::InstOp::MUL:
                blk.insts.emplace_back(InstFR{is_double ? OpFR::FMUL_D : OpFR::FMUL_S, fpr(*rd),
                                              fpr(*lhs), fpr(*rhs)});
                break;
            case ir::InstOp::DIV:
                blk.insts.emplace_back(InstFR{is_double ? OpFR::FDIV_D : OpFR::FDIV_S, fpr(*rd),
                                              fpr(*lhs), fpr(*rhs)});
                break;
            case ir::InstOp::LT:
                blk.insts.emplace_back(
                    InstFR{is_double ? OpFR::FLT_D : OpFR::FLT_S, fpr(*rd), fpr(*lhs), fpr(*rhs)});
                break;
            case ir::InstOp::GT:
                blk.insts.emplace_back(
                    InstFR{is_double ? OpFR::FLT_D : OpFR::FLT_S, fpr(*rd), fpr(*rhs), fpr(*lhs)});
                break;
            case ir::InstOp::LEQ:
                blk.insts.emplace_back(
                    InstFR{is_double ? OpFR::FLE_D : OpFR::FLE_S, fpr(*rd), fpr(*lhs), fpr(*rhs)});
                break;
            case ir::InstOp::GEQ:
                blk.insts.emplace_back(
                    InstFR{is_double ? OpFR::FLE_D : OpFR::FLE_S, fpr(*rd), fpr(*rhs), fpr(*lhs)});
                break;
            case ir::InstOp::EQ:
                blk.insts.emplace_back(
                    InstFR{is_double ? OpFR::FEQ_D : OpFR::FEQ_S, fpr(*rd), fpr(*lhs), fpr(*rhs)});
                break;
            case ir::InstOp::NEQ: {
                blk.insts.emplace_back(
                    InstFR{is_double ? OpFR::FEQ_D : OpFR::FEQ_S, fpr(*rd), fpr(*lhs), fpr(*rhs)});
                blk.insts.emplace_back(InstI{OpI::XORI, gpr(*rd), gpr(*rd), 1});
                break;
            }
            default: break;
        }
        return;
    }

    if (lhs && rhs) {
        // Integer R-type instructions
        auto emit_r = [&](OpR op, OpI opw) {
            if (w)
                blk.insts.emplace_back(InstI{opw, gpr(*rd), gpr(*lhs), (int32_t)(*rhs)});
            else
                blk.insts.emplace_back(InstR{op, gpr(*rd), gpr(*lhs), gpr(*rhs)});
        };
        switch (inst.op) {
            case ir::InstOp::ADD: emit_r(OpR::ADD, OpI::ADDW); break;
            case ir::InstOp::SUB: emit_r(OpR::SUB, OpI::SUBW); break;
            case ir::InstOp::MUL: emit_r(OpR::MUL, OpI::MULW); break;
            case ir::InstOp::DIV: emit_r(OpR::DIV, OpI::DIVW); break;
            case ir::InstOp::MOD: emit_r(OpR::REM, OpI::REMW); break;
            case ir::InstOp::AND:
                blk.insts.emplace_back(InstR{OpR::AND, gpr(*rd), gpr(*lhs), gpr(*rhs)});
                break;
            case ir::InstOp::OR:
                blk.insts.emplace_back(InstR{OpR::OR, gpr(*rd), gpr(*lhs), gpr(*rhs)});
                break;
            case ir::InstOp::LT:
                blk.insts.emplace_back(InstR{OpR::SLT, gpr(*rd), gpr(*lhs), gpr(*rhs)});
                break;
            case ir::InstOp::GT:
                blk.insts.emplace_back(InstR{OpR::SLT, gpr(*rd), gpr(*rhs), gpr(*lhs)});
                break;
            case ir::InstOp::LEQ: {
                blk.insts.emplace_back(InstR{OpR::SLT, gpr(*rd), gpr(*rhs), gpr(*lhs)});
                blk.insts.emplace_back(InstI{OpI::XORI, gpr(*rd), gpr(*rd), 1});
                break;
            }
            case ir::InstOp::GEQ: {
                blk.insts.emplace_back(InstR{OpR::SLT, gpr(*rd), gpr(*lhs), gpr(*rhs)});
                blk.insts.emplace_back(InstI{OpI::XORI, gpr(*rd), gpr(*rd), 1});
                break;
            }
            case ir::InstOp::EQ: {
                blk.insts.emplace_back(InstR{OpR::SUB, gpr(*rd), gpr(*lhs), gpr(*rhs)});
                blk.insts.emplace_back(PseudoR{PseudoR::SEQZ, gpr(*rd), gpr(*rd)});
                break;
            }
            case ir::InstOp::NEQ: {
                blk.insts.emplace_back(InstR{OpR::SUB, gpr(*rd), gpr(*lhs), gpr(*rhs)});
                blk.insts.emplace_back(PseudoR{PseudoR::SNEZ, gpr(*rd), gpr(*rd)});
                break;
            }
            default: break;
        }
        return;
    }

    // Integer with immediate operand
    if (lhs && rhs_cv) {
        auto imm_opt = extract_imm(*rhs_cv);
        if (!imm_opt) return;
        auto imm = (int32_t)*imm_opt;
        switch (inst.op) {
            case ir::InstOp::ADD:
                blk.insts.emplace_back(InstI{w ? OpI::ADDIW : OpI::ADDI, gpr(*rd), gpr(*lhs), imm});
                break;
            case ir::InstOp::LT:
                blk.insts.emplace_back(InstI{OpI::SLTI, gpr(*rd), gpr(*lhs), imm});
                break;
            case ir::InstOp::GEQ:
                blk.insts.emplace_back(InstI{OpI::SLTI, gpr(*rd), gpr(*lhs), imm});
                blk.insts.emplace_back(InstI{OpI::XORI, gpr(*rd), gpr(*rd), 1});
                break;
            case ir::InstOp::GT:
                if (imm < 2047) {
                    // x > N ≡ not (x < N+1)
                    blk.insts.emplace_back(InstI{OpI::SLTI, gpr(*rd), gpr(*lhs), imm + 1});
                    blk.insts.emplace_back(InstI{OpI::XORI, gpr(*rd), gpr(*rd), 1});
                } else {
                    // x > N ≡ N < x
                    auto t0 = GeneralReg::fromString("t0");
                    blk.insts.emplace_back(PseudoLI{t0, imm});
                    blk.insts.emplace_back(InstR{OpR::SLT, gpr(*rd), t0, gpr(*lhs)});
                }
                break;
            case ir::InstOp::LEQ:
                if (imm < 2047) {
                    // x <= N ≡ not (x < N+1)
                    blk.insts.emplace_back(InstI{OpI::SLTI, gpr(*rd), gpr(*lhs), imm + 1});
                    blk.insts.emplace_back(InstI{OpI::XORI, gpr(*rd), gpr(*rd), 1});
                } else {
                    // x <= N ≡ not (N < x)
                    auto t0 = GeneralReg::fromString("t0");
                    blk.insts.emplace_back(PseudoLI{t0, imm});
                    blk.insts.emplace_back(InstR{OpR::SLT, gpr(*rd), t0, gpr(*lhs)});
                    blk.insts.emplace_back(InstI{OpI::XORI, gpr(*rd), gpr(*rd), 1});
                }
                break;
            case ir::InstOp::EQ:
                // x == c → t0 = c; rd = x - t0; rd = (rd == 0)
                blk.insts.emplace_back(PseudoLI{gpr(5), imm});
                blk.insts.emplace_back(InstR{OpR::SUB, gpr(*rd), gpr(*lhs), gpr(5)});
                blk.insts.emplace_back(PseudoR{PseudoR::SEQZ, gpr(*rd), gpr(*rd)});
                break;
            case ir::InstOp::NEQ:
                // x != c → t0 = c; rd = x - t0; rd = (rd != 0)
                blk.insts.emplace_back(PseudoLI{gpr(5), imm});
                blk.insts.emplace_back(InstR{OpR::SUB, gpr(*rd), gpr(*lhs), gpr(5)});
                blk.insts.emplace_back(PseudoR{PseudoR::SNEZ, gpr(*rd), gpr(*rd)});
                break;
            case ir::InstOp::SUB:
                blk.insts.emplace_back(
                    InstI{w ? OpI::ADDIW : OpI::ADDI, gpr(*rd), gpr(*lhs), (int32_t)(-imm)});
                break;
            case ir::InstOp::MUL:
            case ir::InstOp::DIV:
            case ir::InstOp::MOD:
            case ir::InstOp::AND:
            case ir::InstOp::OR: {
                // LI imm into temp register (t0 if rd == lhs to avoid clobbering lhs)
                bool clobber = (*rd == *lhs);
                auto tmp = clobber ? gpr(5) : gpr(*rd);
                blk.insts.emplace_back(PseudoLI{tmp, imm});
                OpR op_r;
                OpI op_w;
                switch (inst.op) {
                    case ir::InstOp::MUL:
                        op_r = OpR::MUL;
                        op_w = OpI::MULW;
                        break;
                    case ir::InstOp::DIV:
                        op_r = OpR::DIV;
                        op_w = OpI::DIVW;
                        break;
                    case ir::InstOp::MOD:
                        op_r = OpR::REM;
                        op_w = OpI::REMW;
                        break;
                    case ir::InstOp::AND:
                        blk.insts.emplace_back(InstR{OpR::AND, gpr(*rd), gpr(*lhs), tmp});
                        return;
                    case ir::InstOp::OR:
                        blk.insts.emplace_back(InstR{OpR::OR, gpr(*rd), gpr(*lhs), tmp});
                        return;
                    default: return;
                }
                if (w)
                    blk.insts.emplace_back(InstI{op_w, gpr(*rd), gpr(*lhs), (int32_t)(*rd)});
                else
                    blk.insts.emplace_back(InstR{op_r, gpr(*rd), gpr(*lhs), tmp});
                break;
            }
            default: break;
        }
        return;
    }

    // register RHS + constexpr LHS
    auto lhs_cv = std::get_if<ir::ConstexprValue>(&inst.lhs);
    if (lhs_cv && rhs) {
        auto imm_opt = extract_imm(*lhs_cv);
        if (!imm_opt) return;
        int64_t cv = *imm_opt;
        switch (inst.op) {
            // commutative — treat like register+const on RHS
            case ir::InstOp::ADD:
                blk.insts.emplace_back(
                    InstI{w ? OpI::ADDIW : OpI::ADDI, gpr(*rd), gpr(*rhs), (int32_t)cv});
                break;
            case ir::InstOp::MUL:
            case ir::InstOp::AND:
            case ir::InstOp::OR:
            case ir::InstOp::EQ:
            case ir::InstOp::NEQ: {
                // Use t0 to avoid clobbering rhs when rd == rhs
                auto tmp = (*rd == *rhs) ? gpr(5) : gpr(*rd);
                blk.insts.emplace_back(PseudoLI{tmp, cv});
                switch (inst.op) {
                    case ir::InstOp::MUL:
                        if (w)
                            blk.insts.emplace_back(
                                InstI{OpI::MULW, gpr(*rd), gpr(*rhs), (int32_t)(*rd)});
                        else
                            blk.insts.emplace_back(InstR{OpR::MUL, gpr(*rd), gpr(*rhs), tmp});
                        break;
                    case ir::InstOp::AND:
                        blk.insts.emplace_back(InstR{OpR::AND, gpr(*rd), gpr(*rhs), tmp});
                        break;
                    case ir::InstOp::OR:
                        blk.insts.emplace_back(InstR{OpR::OR, gpr(*rd), gpr(*rhs), tmp});
                        break;
                    case ir::InstOp::EQ:
                        blk.insts.emplace_back(InstR{OpR::SUB, gpr(*rd), gpr(*rhs), tmp});
                        blk.insts.emplace_back(PseudoR{PseudoR::SEQZ, gpr(*rd), gpr(*rd)});
                        break;
                    case ir::InstOp::NEQ:
                        blk.insts.emplace_back(InstR{OpR::SUB, gpr(*rd), gpr(*rhs), tmp});
                        blk.insts.emplace_back(PseudoR{PseudoR::SNEZ, gpr(*rd), gpr(*rd)});
                        break;
                    default: break;
                }
                break;
            }
            // non-commutative: load const, then R-type
            case ir::InstOp::SUB:
            case ir::InstOp::DIV:
            case ir::InstOp::MOD: {
                // Use t0 to avoid clobbering rhs when rd == rhs
                auto tmp = (*rd == *rhs) ? gpr(5) : gpr(*rd);
                blk.insts.emplace_back(PseudoLI{tmp, cv});
                switch (inst.op) {
                    case ir::InstOp::SUB:
                        if (w)
                            blk.insts.emplace_back(
                                InstI{OpI::SUBW, gpr(*rd), tmp, (int32_t)(*rhs)});
                        else
                            blk.insts.emplace_back(InstR{OpR::SUB, gpr(*rd), tmp, gpr(*rhs)});
                        break;
                    case ir::InstOp::DIV:
                        if (w)
                            blk.insts.emplace_back(
                                InstI{OpI::DIVW, gpr(*rd), tmp, (int32_t)(*rhs)});
                        else
                            blk.insts.emplace_back(InstR{OpR::DIV, gpr(*rd), tmp, gpr(*rhs)});
                        break;
                    case ir::InstOp::MOD:
                        if (w)
                            blk.insts.emplace_back(
                                InstI{OpI::REMW, gpr(*rd), tmp, (int32_t)(*rhs)});
                        else
                            blk.insts.emplace_back(InstR{OpR::REM, gpr(*rd), tmp, gpr(*rhs)});
                        break;
                    default: break;
                }
                break;
            }
            case ir::InstOp::GT: {
                // c > x ≡ x < c → SLTI if c fits, else PseudoLI + SLT
                if ((int32_t)cv == cv && cv < 2047) {
                    blk.insts.emplace_back(InstI{OpI::SLTI, gpr(*rd), gpr(*rhs), (int32_t)cv + 1});
                    blk.insts.emplace_back(InstI{OpI::XORI, gpr(*rd), gpr(*rd), 1});
                } else {
                    auto t0 = GeneralReg::fromString("t0");
                    blk.insts.emplace_back(PseudoLI{t0, cv});
                    blk.insts.emplace_back(InstR{OpR::SLT, gpr(*rd), t0, gpr(*rhs)});
                }
                break;
            }
            case ir::InstOp::LEQ: {
                // c <= x ≡ x >= c ≡ !(x < c) → SLTI + XORI if c fits, else PseudoLI + SLT + XORI
                if ((int32_t)cv == cv && cv < 2047) {
                    blk.insts.emplace_back(InstI{OpI::SLTI, gpr(*rd), gpr(*rhs), (int32_t)cv + 1});
                    blk.insts.emplace_back(InstI{OpI::XORI, gpr(*rd), gpr(*rd), 1});
                } else {
                    auto t0 = GeneralReg::fromString("t0");
                    blk.insts.emplace_back(PseudoLI{t0, cv});
                    blk.insts.emplace_back(InstR{OpR::SLT, gpr(*rd), t0, gpr(*rhs)});
                    blk.insts.emplace_back(InstI{OpI::XORI, gpr(*rd), gpr(*rd), 1});
                }
                break;
            }
            case ir::InstOp::LT: {
                // c < x → PseudoLI(t0, c) + SLT(rd, t0, x)
                auto t0 = GeneralReg::fromString("t0");
                blk.insts.emplace_back(PseudoLI{t0, cv});
                blk.insts.emplace_back(InstR{OpR::SLT, gpr(*rd), t0, gpr(*rhs)});
                break;
            }
            case ir::InstOp::GEQ: {
                // c >= x ≡ !(c < x) → PseudoLI(t0, c) + SLT(rd, t0, x) + XORI(rd, rd, 1)
                auto t0 = GeneralReg::fromString("t0");
                blk.insts.emplace_back(PseudoLI{t0, cv});
                blk.insts.emplace_back(InstR{OpR::SLT, gpr(*rd), t0, gpr(*rhs)});
                blk.insts.emplace_back(InstI{OpI::XORI, gpr(*rd), gpr(*rd), 1});
                break;
            }
            default: break;
        }
        return;
    }
}

inline void translate_call(const ir::CallInst& inst, AsmBlock& blk) {
    PseudoJ pi{PseudoJ::CALL, name_of(inst.func.def)};
    blk.insts.emplace_back(pi);
}

inline void translate_inst(const ir::Inst& inst, AsmBlock& blk, const FrameLayout& frame,
                           const ir::lowering::TargetABI& abi, const ir::lowering::ColorMap& regs) {
    Match{inst}([&](const ir::UnaryInst& u) { translate_unary(u, blk, frame, abi, regs); },
                [&](const ir::BinaryInst& b) { translate_binary(b, blk, frame, abi, regs); },
                [&](const ir::CallInst& c) { translate_call(c, blk); },
                [&](const ir::PhiInst&) {
                    throw COMPILER_ERROR("PhiInst should be eliminated before isel");
                });
}

inline void translate_exit(const ir::Exit& exit, AsmBlock& blk, const std::string& func_name,
                           const ir::lowering::ColorMap& regs) {
    Match{exit}(
        [&](const ir::ReturnExit&) {
            blk.insts.emplace_back(PseudoJ{PseudoJ::J, ".L" + func_name + "_epilogue"});
        },
        [&](const ir::JumpExit& j) {
            blk.insts.emplace_back(PseudoJ{PseudoJ::J, block_label(func_name, j.target->label)});
        },
        [&](const ir::BranchExit& b) {
            auto cond = lookup_reg(regs, b.cond);
            if (!cond) throw COMPILER_ERROR("Branch condition must be GPR");
            blk.insts.emplace_back(PseudoB{PseudoB::BNEZ, GeneralReg{static_cast<uint8_t>(*cond)},
                                           block_label(func_name, b.true_target->label)});
            blk.insts.emplace_back(
                PseudoJ{PseudoJ::J, block_label(func_name, b.false_target->label)});
        });
}

inline AsmFunc translate_func(const ir::Func& func, const ir::lowering::TargetABI& abi,
                              const ir::lowering::ColorMap& regs) {
    AsmFunc af;
    af.name = func.name;
    af.frame = compute_frame(func, abi);
    auto entry_lbl = ".L" + func.name + "_entry";
    auto epi_lbl = ".L" + func.name + "_epilogue";

    // entry block: function label + prologue
    {
        AsmBlock entry;
        entry.label = func.name;  // function entry label
        if (af.frame.total_size > 0) {
            entry.insts.emplace_back(
                InstI{OpI::ADDI, GeneralReg{2}, GeneralReg{2}, -(int32_t)af.frame.total_size});
        }

        // Emit Alloc initializer stores (after stack frame is allocated, before entry)
        for (auto& local : func.locals()) {
            if (!local->init || !af.frame.has_spill(local.get())) continue;
            size_t off = af.frame.offset_of(local.get());

            // byte-buffer case (array of init data)
            if (auto* buf = std::get_if<std::unique_ptr<std::byte[]>>(&local->init->val)) {
                size_t sz = abi.mem.size(local->type);
                for (size_t i = 0; i < sz; i += 8) {
                    int64_t v = 0;
                    size_t chunk = (sz - i < 8) ? (sz - i) : 8;
                    // For sub-8-byte tail, use SW/SB instead of SD
                    if (chunk == 8) {
                        std::memcpy(&v, buf->get() + i, 8);
                        entry.insts.emplace_back(PseudoLI{gpr(5), v});
                        entry.insts.emplace_back(
                            InstI{OpI::SD, gpr(5), GeneralReg{2}, (int32_t)(off + i)});
                    } else if (chunk == 4) {
                        int32_t v32 = 0;
                        std::memcpy(&v32, buf->get() + i, 4);
                        entry.insts.emplace_back(PseudoLI{gpr(5), (int64_t)v32});
                        entry.insts.emplace_back(
                            InstI{OpI::SW, gpr(5), GeneralReg{2}, (int32_t)(off + i)});
                    } else if (chunk == 1) {
                        entry.insts.emplace_back(PseudoLI{gpr(5), (int64_t)buf->get()[i]});
                        entry.insts.emplace_back(
                            InstI{OpI::SB, gpr(5), GeneralReg{2}, (int32_t)(off + i)});
                    }
                }
                continue;
            }

            // scalar case
            if (std::holds_alternative<std::monostate>(local->init->val)) continue;
            int64_t val = 0;
            size_t sz = abi.mem.size(local->type);
            if (auto* v = std::get_if<int>(&local->init->val))
                val = *v;
            else if (auto* v = std::get_if<int64_t>(&local->init->val))
                val = *v;
            else if (auto* v = std::get_if<float>(&local->init->val))
                std::memcpy(&val, v, 4);
            else if (auto* v = std::get_if<double>(&local->init->val))
                std::memcpy(&val, v, 8);
            else if (auto* v = std::get_if<bool>(&local->init->val))
                val = *v ? 1 : 0;
            else
                continue;

            OpI op = (sz == 4) ? OpI::SW : (sz == 1) ? OpI::SB : OpI::SD;
            entry.insts.emplace_back(PseudoLI{gpr(5), val});
            entry.insts.emplace_back(InstI{op, gpr(5), GeneralReg{2}, (int32_t)off});
        }

        entry.insts.emplace_back(PseudoJ{PseudoJ::J, entry_lbl});
        af.blocks.emplace_back(std::move(entry));
    }

    // translate IR blocks
    for (auto& blk : func.blocks()) {
        AsmBlock ab;
        ab.label = block_label(func.name, blk->label);
        for (auto& inst : blk->insts()) {
            translate_inst(inst, ab, af.frame, abi, regs);
        }
        translate_exit(blk->exit(), ab, func.name, regs);
        af.blocks.emplace_back(std::move(ab));
    }

    // epilogue block
    {
        AsmBlock epi;
        epi.label = epi_lbl;
        if (af.frame.total_size > 0) {
            epi.insts.emplace_back(
                InstI{OpI::ADDI, GeneralReg{2}, GeneralReg{2}, (int32_t)af.frame.total_size});
        }
        epi.insts.emplace_back(PseudoRet{});
        af.blocks.emplace_back(std::move(epi));
    }

    return af;
}

inline Module lower(const ir::Program& prog, const ir::lowering::ColorMap& regs,
                    const ir::lowering::TargetABI& abi) {
    Module mod;

    // collect user globals (exclude register proxies)
    for (auto& g : prog.globals()) {
        if (regs.count(g->value())) continue;
        Global gl{g->name, g->type, g->init, g->comptime};
        mod.globals.emplace_back(std::move(gl));
    }

    // translate functions
    for (auto& f : prog.funcs()) {
        mod.funcs.emplace_back(translate_func(*f, abi, regs));
    }

    return mod;
}

}  // namespace rv64::isel
