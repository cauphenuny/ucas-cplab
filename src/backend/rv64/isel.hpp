#pragma once

#include "backend/ir/ir.h"
#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/lowering/regalloc/precolorize.hpp"
#include "backend/ir/type.hpp"
#include "inst.hpp"
#include "utils/match.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace rv64::isel {

inline std::string name_of(const ir::NameDef& def) {
    return Match{def}([&](const auto* p) -> std::string { return p->name; });
}

// Look up a LeftValue in the register ColorMap (returns register ID, or nullopt if unnamed
// ref/comptime)
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
    // sanitize block label for asm: 'entry → _entry, 'while_cond_1 → _while_cond_1
    std::string sanitized = blk_label;
    if (!sanitized.empty() && sanitized[0] == '\'') sanitized = sanitized.substr(1);
    return ".L_" + func_name + "_" + sanitized;
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
                // comptime Alloc: load init value into GPR
                if (!fp && alloc->comptime && alloc->init) {
                    if (auto imm = extract_imm(*alloc->init)) {
                        blk.insts.emplace_back(PseudoLI{gpr(*rd), *imm});
                    }
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
            if (ref_alloc && frame.has_spill(ref_alloc)) {
                size_t off = frame.offset_of(ref_alloc);
                blk.insts.emplace_back(
                    InstI{is_32bit_op(ir::type_of(*inst.result)) ? OpI::LW : OpI::LD, gpr(*rd),
                          GeneralReg{2}, (int32_t)off});
            } else if (ref_alloc) {
                auto sz = abi.mem.size(ref_alloc->type);
                PseudoL pi{sz == 8 ? PseudoL::LGD : PseudoL::LGW, gpr(*rd), ref_alloc->name};
                blk.insts.emplace_back(pi);
            } else if (auto base = lookup_reg(regs, inst.operand)) {
                // register dereference (value in a register)
                blk.insts.emplace_back(
                    InstI{is_32bit_op(ir::type_of(*inst.result)) ? OpI::LW : OpI::LD, gpr(*rd),
                          gpr(*base), 0});
            }
            break;
        }
        case ir::UnaryInstOp::BORROW:
        case ir::UnaryInstOp::BORROW_MUT:
            throw std::runtime_error("BORROW should be lowered by AddressLowering");
        case ir::UnaryInstOp::CONVERT:
            if (auto src = lookup_reg(regs, inst.operand)) {
                // int-to-int conversion (e.g. i32 -> int sign extension)
                if (is_32bit_op(ir::type_of(inst.operand)) &&
                    !is_32bit_op(ir::type_of(*inst.result))) {
                    blk.insts.emplace_back(InstI{OpI::ADDIW, gpr(*rd), gpr(*src), 0});
                } else {
                    blk.insts.emplace_back(PseudoR{PseudoR::MV, gpr(*rd), gpr(*src)});
                }
            }
            break;
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
        if (ref_alloc && frame.has_spill(ref_alloc) && val) {
            // spill slot store: offset(sp)
            size_t off = frame.offset_of(ref_alloc);
            blk.insts.emplace_back(InstI{is_32bit_op(ir::type_of(inst.rhs)) ? OpI::SW : OpI::SD,
                                         gpr(*val), GeneralReg{2}, (int32_t)off});
        } else if (ref_alloc && val) {
            // global store (register proxies are covered by register-to-register MOVs)
            auto sz = abi.mem.size(ref_alloc->type);
            PseudoL pi{sz == 8 ? PseudoL::SGD : PseudoL::SGW, gpr(*val), ref_alloc->name};
            blk.insts.emplace_back(pi);
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
                // x > N ≡ not (x < N+1)
                if (imm < 2047) {
                    blk.insts.emplace_back(InstI{OpI::SLTI, gpr(*rd), gpr(*lhs), imm + 1});
                    blk.insts.emplace_back(InstI{OpI::XORI, gpr(*rd), gpr(*rd), 1});
                } else {
                    // x > N when N ≥ 2047: materialize N and use register SLT
                }
                break;
            case ir::InstOp::LEQ:
                if (imm < 2047) {
                    blk.insts.emplace_back(InstI{OpI::SLTI, gpr(*rd), gpr(*lhs), imm + 1});
                }
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
                // LI imm into rd, then R-type with rd as rhs
                blk.insts.emplace_back(PseudoLI{gpr(*rd), imm});
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
                        blk.insts.emplace_back(InstR{OpR::AND, gpr(*rd), gpr(*lhs), gpr(*rd)});
                        return;
                    case ir::InstOp::OR:
                        blk.insts.emplace_back(InstR{OpR::OR, gpr(*rd), gpr(*lhs), gpr(*rd)});
                        return;
                    default: return;
                }
                if (w)
                    blk.insts.emplace_back(InstI{op_w, gpr(*rd), gpr(*lhs), (int32_t)(*rd)});
                else
                    blk.insts.emplace_back(InstR{op_r, gpr(*rd), gpr(*lhs), gpr(*rd)});
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
                    throw std::runtime_error("PhiInst should be eliminated before isel");
                });
}

inline void translate_exit(const ir::Exit& exit, AsmBlock& blk, const std::string& func_name,
                           const ir::lowering::ColorMap& regs) {
    Match{exit}(
        [&](const ir::ReturnExit&) {
            blk.insts.emplace_back(PseudoJ{PseudoJ::J, ".L_" + func_name + "_epilogue"});
        },
        [&](const ir::JumpExit& j) {
            blk.insts.emplace_back(PseudoJ{PseudoJ::J, block_label(func_name, j.target->label)});
        },
        [&](const ir::BranchExit& b) {
            auto cond = lookup_reg(regs, b.cond);
            if (!cond) throw std::runtime_error("Branch condition must be GPR");
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
    auto entry_lbl = ".L_" + func.name + "_entry";
    auto epi_lbl = ".L_" + func.name + "_epilogue";

    // entry block: function label + prologue
    {
        AsmBlock entry;
        entry.label = func.name;  // function entry label
        if (af.frame.total_size > 0) {
            entry.insts.emplace_back(
                InstI{OpI::ADDI, GeneralReg{2}, GeneralReg{2}, -(int32_t)af.frame.total_size});
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
