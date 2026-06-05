#pragma once

#include "abi.hpp"
#include "inst.hpp"
#include "module.hpp"
#include "backend/ir/ir.h"
#include "backend/ir/type.hpp"
#include "backend/ir/lowering/abi.hpp"
#include "utils/diagnosis.hpp"
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

inline bool is_reg_proxy(const ir::Alloc& a) {
    return a.name.rfind("__reg_", 0) == 0;
}

inline std::optional<GeneralReg> parse_gpr(const std::string& name) {
    static const std::unordered_map<std::string, uint8_t> gpr_map = {
        {"__reg_zero", 0}, {"__reg_ra", 1}, {"__reg_sp", 2}, {"__reg_gp", 3},
        {"__reg_tp", 4},   {"__reg_t0", 5}, {"__reg_t1", 6}, {"__reg_t2", 7},
        {"__reg_s0", 8},   {"__reg_s1", 9}, {"__reg_a0", 10}, {"__reg_a1", 11},
        {"__reg_a2", 12},  {"__reg_a3", 13}, {"__reg_a4", 14}, {"__reg_a5", 15},
        {"__reg_a6", 16},  {"__reg_a7", 17}, {"__reg_s2", 18}, {"__reg_s3", 19},
        {"__reg_s4", 20},  {"__reg_s5", 21}, {"__reg_s6", 22}, {"__reg_s7", 23},
        {"__reg_s8", 24},  {"__reg_s9", 25}, {"__reg_s10", 26}, {"__reg_s11", 27},
        {"__reg_t3", 28},  {"__reg_t4", 29}, {"__reg_t5", 30}, {"__reg_t6", 31},
    };
    auto it = gpr_map.find(name);
    if (it != gpr_map.end()) return GeneralReg{it->second};
    return std::nullopt;
}

inline std::optional<FloatReg> parse_fpr(const std::string& name) {
    // __reg_fa0 → fa0 → f10, __reg_fs0 → fs0 → f8, etc.
    static const std::unordered_map<std::string, uint8_t> fpr_map = {
        {"__reg_ft0", 0},   {"__reg_ft1", 1},   {"__reg_ft2", 2},   {"__reg_ft3", 3},
        {"__reg_ft4", 4},   {"__reg_ft5", 5},   {"__reg_ft6", 6},   {"__reg_ft7", 7},
        {"__reg_fs0", 8},   {"__reg_fs1", 9},   {"__reg_fa0", 10},  {"__reg_fa1", 11},
        {"__reg_fa2", 12},  {"__reg_fa3", 13},  {"__reg_fa4", 14},  {"__reg_fa5", 15},
        {"__reg_fa6", 16},  {"__reg_fa7", 17},  {"__reg_fs2", 18},  {"__reg_fs3", 19},
        {"__reg_fs4", 20},  {"__reg_fs5", 21},  {"__reg_fs6", 22},  {"__reg_fs7", 23},
        {"__reg_fs8", 24},  {"__reg_fs9", 25},  {"__reg_fs10", 26}, {"__reg_fs11", 27},
        {"__reg_ft8", 28},  {"__reg_ft9", 29},  {"__reg_ft10", 30}, {"__reg_ft11", 31},
    };
    auto it = fpr_map.find(name);
    if (it != fpr_map.end()) return FloatReg{it->second};
    return std::nullopt;
}

inline std::optional<int64_t> extract_imm(const ir::ConstexprValue& cv) {
    using namespace ir::type;
    int64_t val = 0;
    bool ok = false;
    Match{cv.val}(
        [&](int v) { val = v; ok = true; },
        [&](int64_t v) { val = v; ok = true; },
        [&](bool v) { val = v ? 1 : 0; ok = true; },
        [&](auto&&) {});
    if (ok) return val;
    return std::nullopt;
}

inline bool is_32bit_op(const ir::Type& t) {
    using namespace ir::type;
    return t.is<Primitive>() &&
           (std::holds_alternative<Int32>(t.as<Primitive>()) ||
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

inline std::optional<GeneralReg> resolve_gpr(const ir::Value& val) {
    if (auto* lv = std::get_if<ir::LeftValue>(&val)) {
        if (auto* nv = std::get_if<ir::NamedValue>(&*lv)) {
            return parse_gpr(name_of(nv->def));
        }
    }
    return std::nullopt;
}

inline std::optional<GeneralReg> resolve_gpr(const ir::LeftValue& lv) {
    if (auto* nv = std::get_if<ir::NamedValue>(&lv)) {
        return parse_gpr(name_of(nv->def));
    }
    return std::nullopt;
}

inline std::optional<FloatReg> resolve_fpr(const ir::Value& val) {
    if (auto* lv = std::get_if<ir::LeftValue>(&val)) {
        if (auto* nv = std::get_if<ir::NamedValue>(&*lv)) {
            return parse_fpr(name_of(nv->def));
        }
    }
    return std::nullopt;
}

inline std::optional<FloatReg> resolve_fpr(const ir::LeftValue& lv) {
    if (auto* nv = std::get_if<ir::NamedValue>(&lv)) {
        return parse_fpr(name_of(nv->def));
    }
    return std::nullopt;
}

// Check if NamedValue points to a specific global/alloc (not a register proxy)
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
        if (is_reg_proxy(*local)) continue;
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
                           const ir::lowering::TargetABI& abi);
inline void translate_exit(const ir::Exit& exit, AsmBlock& blk, const std::string& func_name);

inline void translate_unary(const ir::UnaryInst& inst, AsmBlock& blk, const FrameLayout& frame,
                            const ir::lowering::TargetABI& abi) {
    using namespace ir::type;
    auto rd = resolve_gpr(inst.operand);  // placeholder, actual rd from inst.result
    (void)rd;
    (void)frame;
    (void)abi;

    if (!inst.result) return;  // no destination, skip (shouldn't happen for non-STORE)

    auto dst_gpr = resolve_gpr(*inst.result);
    auto dst_fpr = resolve_fpr(*inst.result);

    switch (inst.op) {
        case ir::UnaryInstOp::MOV: {
            if (auto* cv = std::get_if<ir::ConstexprValue>(&inst.operand)) {
                if (auto imm = extract_imm(*cv)) {
                    if (dst_gpr) {
                        blk.insts.push_back(PseudoInst{Pseudo::LI, *dst_gpr, GeneralReg{0}, *imm});
                    } else if (dst_fpr) {
                        // float immediate: needs literal pool, handled later
                        throw std::runtime_error("float immediate MOV not yet supported");
                    }
                }
            } else if (auto src_gpr = resolve_gpr(inst.operand)) {
                if (dst_gpr && src_gpr->id != dst_gpr->id) {
                    blk.insts.push_back(PseudoInst{Pseudo::MV, *dst_gpr, *src_gpr});
                }
            } else if (auto src_fpr = resolve_fpr(inst.operand)) {
                if (dst_fpr && src_fpr->id != dst_fpr->id) {
                    // use fsgnj.s/d as float move
                    bool is_double = !is_32bit_op(ir::type_of(*inst.result));
                    blk.insts.push_back(
                        InstFR{is_double ? OpFR::FSGNJ_D : OpFR::FSGNJ_S, *dst_fpr, *src_fpr, *src_fpr});
                }
            }
            break;
        }
        case ir::UnaryInstOp::NEG: {
            if (dst_gpr) {
                auto src = resolve_gpr(inst.operand);
                if (!src) throw std::runtime_error("NEG: expected GPR operand");
                if (is_32bit_op(ir::type_of(*inst.result))) {
                    blk.insts.push_back(PseudoInst{Pseudo::NEGW, *dst_gpr, *src});
                } else {
                    blk.insts.push_back(PseudoInst{Pseudo::NEG, *dst_gpr, *src});
                }
            } else if (dst_fpr) {
                auto src = resolve_fpr(inst.operand);
                if (!src) throw std::runtime_error("NEG: expected FPR operand");
                bool is_double = !is_32bit_op(ir::type_of(*inst.result));
                blk.insts.push_back(
                    InstFR{is_double ? OpFR::FSGNJN_D : OpFR::FSGNJN_S, *dst_fpr, *src, *src});
            }
            break;
        }
        case ir::UnaryInstOp::NOT: {
            if (!dst_gpr) throw std::runtime_error("NOT: expected GPR destination");
            auto src = resolve_gpr(inst.operand);
            if (!src) throw std::runtime_error("NOT: expected GPR operand");
            auto t = ir::type_of(*inst.result);
            if (t.is<Primitive>() && std::holds_alternative<Int1>(t.as<Primitive>())) {
                // bool NOT: x ^ 1
                blk.insts.push_back(InstI{OpI::XORI, *dst_gpr, *src, 1});
            } else {
                blk.insts.push_back(PseudoInst{Pseudo::SEQZ, *dst_gpr, *src});
            }
            break;
        }
        case ir::UnaryInstOp::LOAD: {
            auto* ref_alloc = resolve_alloc(inst.operand);
            if (ref_alloc && frame.has_spill(ref_alloc) && dst_gpr) {
                size_t off = frame.offset_of(ref_alloc);
                blk.insts.push_back(
                    InstI{is_32bit_op(ir::type_of(*inst.result)) ? OpI::LW : OpI::LD,
                          *dst_gpr, GeneralReg{2}, (int32_t)off});
            } else if (ref_alloc && !is_reg_proxy(*ref_alloc) && dst_gpr) {
                PseudoInst pi{Pseudo::LOAD_GLOBAL, *dst_gpr, GeneralReg{0}};
                pi.symbol = ref_alloc->name;
                pi.elem_size = abi.mem.size(ref_alloc->type);
                blk.insts.push_back(pi);
            } else if (ref_alloc && is_reg_proxy(*ref_alloc) && dst_gpr) {
                auto base = resolve_gpr(inst.operand);
                if (base) {
                    blk.insts.push_back(
                        InstI{is_32bit_op(ir::type_of(*inst.result)) ? OpI::LW : OpI::LD,
                              *dst_gpr, *base, 0});
                }
            }
            break;
        }
        case ir::UnaryInstOp::BORROW:
        case ir::UnaryInstOp::BORROW_MUT:
            throw std::runtime_error("BORROW should be lowered by AddressLowering");
        case ir::UnaryInstOp::CONVERT:
            // Handled later with full type conversion logic
            if (dst_gpr) {
                auto src = resolve_gpr(inst.operand);
                if (src) {
                    // int-to-int conversion (e.g. i32 -> int sign extension)
                    if (is_32bit_op(ir::type_of(inst.operand)) &&
                        !is_32bit_op(ir::type_of(*inst.result))) {
                        blk.insts.push_back(InstI{OpI::ADDIW, *dst_gpr, *src, 0});
                    } else {
                        blk.insts.push_back(PseudoInst{Pseudo::MV, *dst_gpr, *src});
                    }
                }
            }
            break;
    }
}

inline void translate_binary(const ir::BinaryInst& inst, AsmBlock& blk, const FrameLayout& frame,
                             const ir::lowering::TargetABI& abi) {
    using namespace ir::type;
    if (!inst.result) {
        // STORE instruction
        if (inst.op != ir::InstOp::STORE) return;
        auto* ref_alloc = resolve_alloc(inst.lhs);
        auto val_gpr = resolve_gpr(inst.rhs);
        if (ref_alloc && frame.has_spill(ref_alloc) && val_gpr) {
            // spill slot store: offset(sp)
            size_t off = frame.offset_of(ref_alloc);
            blk.insts.push_back(
                InstI{is_32bit_op(ir::type_of(inst.rhs)) ? OpI::SW : OpI::SD,
                      *val_gpr, GeneralReg{2}, (int32_t)off});  // x2 = sp
        } else if (ref_alloc && !is_reg_proxy(*ref_alloc) && val_gpr) {
            // global store
            PseudoInst pi{Pseudo::STORE_GLOBAL, GeneralReg{0}, *val_gpr};
            pi.symbol = ref_alloc->name;
            pi.elem_size = abi.mem.size(ref_alloc->type);
            blk.insts.push_back(pi);
        }
        return;
    }

    auto dst_gpr = resolve_gpr(*inst.result);
    auto dst_fpr = resolve_fpr(*inst.result);
    auto t = ir::type_of(*inst.result);

    auto lhs_gpr = resolve_gpr(inst.lhs);
    auto rhs_gpr = resolve_gpr(inst.rhs);
    auto lhs_fpr = resolve_fpr(inst.lhs);
    auto rhs_fpr = resolve_fpr(inst.rhs);
    auto lhs_cv = std::get_if<ir::ConstexprValue>(&inst.lhs);
    auto rhs_cv = std::get_if<ir::ConstexprValue>(&inst.rhs);

    bool w = is_32bit_op(t);
    bool fp = is_fp_op(t);
    bool is_double = fp && !w;  // Float or Float64 → double precision

    if (fp && dst_fpr && lhs_fpr && rhs_fpr) {
        switch (inst.op) {
            case ir::InstOp::ADD:
                blk.insts.push_back(InstFR{is_double ? OpFR::FADD_D : OpFR::FADD_S, *dst_fpr, *lhs_fpr, *rhs_fpr}); break;
            case ir::InstOp::SUB:
                blk.insts.push_back(InstFR{is_double ? OpFR::FSUB_D : OpFR::FSUB_S, *dst_fpr, *lhs_fpr, *rhs_fpr}); break;
            case ir::InstOp::MUL:
                blk.insts.push_back(InstFR{is_double ? OpFR::FMUL_D : OpFR::FMUL_S, *dst_fpr, *lhs_fpr, *rhs_fpr}); break;
            case ir::InstOp::DIV:
                blk.insts.push_back(InstFR{is_double ? OpFR::FDIV_D : OpFR::FDIV_S, *dst_fpr, *lhs_fpr, *rhs_fpr}); break;
            case ir::InstOp::LT:
                blk.insts.push_back(InstFR{is_double ? OpFR::FLT_D : OpFR::FLT_S, *dst_fpr, *lhs_fpr, *rhs_fpr}); break;
            case ir::InstOp::GT:
                blk.insts.push_back(InstFR{is_double ? OpFR::FLT_D : OpFR::FLT_S, *dst_fpr, *rhs_fpr, *lhs_fpr}); break;
            case ir::InstOp::LEQ:
                blk.insts.push_back(InstFR{is_double ? OpFR::FLE_D : OpFR::FLE_S, *dst_fpr, *lhs_fpr, *rhs_fpr}); break;
            case ir::InstOp::GEQ:
                blk.insts.push_back(InstFR{is_double ? OpFR::FLE_D : OpFR::FLE_S, *dst_fpr, *rhs_fpr, *lhs_fpr}); break;
            case ir::InstOp::EQ:
                blk.insts.push_back(InstFR{is_double ? OpFR::FEQ_D : OpFR::FEQ_S, *dst_fpr, *lhs_fpr, *rhs_fpr}); break;
            case ir::InstOp::NEQ: {
                blk.insts.push_back(InstFR{is_double ? OpFR::FEQ_D : OpFR::FEQ_S, *dst_fpr, *lhs_fpr, *rhs_fpr});
                blk.insts.push_back(InstI{OpI::XORI, *dst_gpr, *dst_gpr, 1});
                break;
            }
            default: break;
        }
        return;
    }

    if (dst_gpr && lhs_gpr && rhs_gpr) {
        // Integer R-type instructions
        auto emit_r = [&](OpR op, OpI opw) {
            if (w) blk.insts.push_back(InstI{opw, *dst_gpr, *lhs_gpr, (int32_t)rhs_gpr->id});
            else blk.insts.push_back(InstR{op, *dst_gpr, *lhs_gpr, *rhs_gpr});
        };
        switch (inst.op) {
            case ir::InstOp::ADD: emit_r(OpR::ADD, OpI::ADDW); break;
            case ir::InstOp::SUB: emit_r(OpR::SUB, OpI::SUBW); break;
            case ir::InstOp::MUL: emit_r(OpR::MUL, OpI::MULW); break;
            case ir::InstOp::DIV: emit_r(OpR::DIV, OpI::DIVW); break;
            case ir::InstOp::MOD: emit_r(OpR::REM, OpI::REMW); break;
            case ir::InstOp::AND: blk.insts.push_back(InstR{OpR::AND, *dst_gpr, *lhs_gpr, *rhs_gpr}); break;
            case ir::InstOp::OR:  blk.insts.push_back(InstR{OpR::OR, *dst_gpr, *lhs_gpr, *rhs_gpr}); break;
            case ir::InstOp::LT:  blk.insts.push_back(InstR{OpR::SLT, *dst_gpr, *lhs_gpr, *rhs_gpr}); break;
            case ir::InstOp::GT:  blk.insts.push_back(InstR{OpR::SLT, *dst_gpr, *rhs_gpr, *lhs_gpr}); break;
            case ir::InstOp::LEQ: {
                blk.insts.push_back(InstR{OpR::SLT, *dst_gpr, *rhs_gpr, *lhs_gpr});
                blk.insts.push_back(InstI{OpI::XORI, *dst_gpr, *dst_gpr, 1});
                break;
            }
            case ir::InstOp::GEQ: {
                blk.insts.push_back(InstR{OpR::SLT, *dst_gpr, *lhs_gpr, *rhs_gpr});
                blk.insts.push_back(InstI{OpI::XORI, *dst_gpr, *dst_gpr, 1});
                break;
            }
            case ir::InstOp::EQ: {
                blk.insts.push_back(InstR{OpR::SUB, *dst_gpr, *lhs_gpr, *rhs_gpr});
                blk.insts.push_back(PseudoInst{Pseudo::SEQZ, *dst_gpr, *dst_gpr});
                break;
            }
            case ir::InstOp::NEQ: {
                blk.insts.push_back(InstR{OpR::SUB, *dst_gpr, *lhs_gpr, *rhs_gpr});
                blk.insts.push_back(PseudoInst{Pseudo::SNEZ, *dst_gpr, *dst_gpr});
                break;
            }
            default: break;
        }
        return;
    }

    // Integer with immediate operand
    if (dst_gpr && lhs_gpr && rhs_cv) {
        auto imm_opt = extract_imm(*rhs_cv);
        if (!imm_opt) return;
        int32_t imm = (int32_t)*imm_opt;
        switch (inst.op) {
            case ir::InstOp::ADD:
                blk.insts.push_back(InstI{w ? OpI::ADDIW : OpI::ADDI, *dst_gpr, *lhs_gpr, imm});
                break;
            case ir::InstOp::LT:
                blk.insts.push_back(InstI{OpI::SLTI, *dst_gpr, *lhs_gpr, imm});
                break;
            case ir::InstOp::GEQ:
                blk.insts.push_back(InstI{OpI::SLTI, *dst_gpr, *lhs_gpr, imm});
                blk.insts.push_back(InstI{OpI::XORI, *dst_gpr, *dst_gpr, 1});
                break;
            case ir::InstOp::GT:
                // x > N ≡ not (x < N+1)
                if (imm < 2047) {
                    blk.insts.push_back(InstI{OpI::SLTI, *dst_gpr, *lhs_gpr, imm + 1});
                    blk.insts.push_back(InstI{OpI::XORI, *dst_gpr, *dst_gpr, 1});
                } else {
                    // fallback: use register comparison
                    PseudoInst pi{Pseudo::LI, *dst_gpr, GeneralReg{0}, imm};
                    // need to expand LI here or use a register - simplified for now
                }
                break;
            case ir::InstOp::LEQ:
                if (imm < 2047) {
                    blk.insts.push_back(InstI{OpI::SLTI, *dst_gpr, *lhs_gpr, imm + 1});
                }
                break;
            case ir::InstOp::SUB:
                blk.insts.push_back(InstI{w ? OpI::ADDIW : OpI::ADDI, *dst_gpr, *lhs_gpr, (int32_t)(-imm)});
                break;
            default: break;
        }
        return;
    }
}

inline void translate_call(const ir::CallInst& inst, AsmBlock& blk) {
    PseudoInst pi{Pseudo::CALL, GeneralReg{0}, GeneralReg{0}};
    pi.target = name_of(inst.func.def);
    blk.insts.push_back(pi);
}

inline void translate_inst(const ir::Inst& inst, AsmBlock& blk, const FrameLayout& frame,
                           const ir::lowering::TargetABI& abi) {
    Match{inst}(
        [&](const ir::UnaryInst& u) { translate_unary(u, blk, frame, abi); },
        [&](const ir::BinaryInst& b) { translate_binary(b, blk, frame, abi); },
        [&](const ir::CallInst& c) { translate_call(c, blk); },
        [&](const ir::PhiInst&) {
            throw std::runtime_error("PhiInst should be eliminated before isel");
        });
}

inline void translate_exit(const ir::Exit& exit, AsmBlock& blk, const std::string& func_name) {
    Match{exit}(
        [&](const ir::ReturnExit&) {
            blk.insts.push_back(PseudoInst{Pseudo::J, GeneralReg{0}, GeneralReg{0}, 0,
                                          ".L_" + func_name + "_epilogue"});
        },
        [&](const ir::JumpExit& j) {
            blk.insts.push_back(PseudoInst{Pseudo::J, GeneralReg{0}, GeneralReg{0}, 0,
                                          block_label(func_name, j.target->label)});
        },
        [&](const ir::BranchExit& b) {
            auto cond = resolve_gpr(b.cond);
            if (!cond) throw std::runtime_error("Branch condition must be GPR");
            blk.insts.push_back(PseudoInst{Pseudo::BNEZ, *cond, GeneralReg{0}, 0,
                                          block_label(func_name, b.true_target->label)});
            blk.insts.push_back(PseudoInst{Pseudo::J, GeneralReg{0}, GeneralReg{0}, 0,
                                          block_label(func_name, b.false_target->label)});
        });
}

inline AsmFunc translate_func(const ir::Func& func, const ir::lowering::TargetABI& abi) {
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
            entry.insts.push_back(
                InstI{OpI::ADDI, GeneralReg{2}, GeneralReg{2}, -(int32_t)af.frame.total_size});
        }
        entry.insts.push_back(PseudoInst{Pseudo::J, GeneralReg{0}, GeneralReg{0}, 0, entry_lbl});
        af.blocks.push_back(std::move(entry));
    }

    // translate IR blocks
    for (auto& blk : func.blocks()) {
        AsmBlock ab;
        ab.label = block_label(func.name, blk->label);
        for (auto& inst : blk->insts()) {
            translate_inst(inst, ab, af.frame, abi);
        }
        translate_exit(blk->exit(), ab, func.name);
        af.blocks.push_back(std::move(ab));
    }

    // epilogue block
    {
        AsmBlock epi;
        epi.label = epi_lbl;
        epi.insts.push_back(PseudoInst{Pseudo::RET, GeneralReg{0}, GeneralReg{0}});
        if (af.frame.total_size > 0) {
            // frame dealloc happens before ret, so insert before RET
            epi.insts.insert(epi.insts.begin(),
                InstI{OpI::ADDI, GeneralReg{2}, GeneralReg{2}, (int32_t)af.frame.total_size});
        }
        af.blocks.push_back(std::move(epi));
    }

    return af;
}

inline Module lower(const ir::Program& prog, const ir::lowering::TargetABI& abi) {
    Module mod;

    // collect user globals (exclude register proxies)
    for (auto& g : prog.globals()) {
        if (is_reg_proxy(*g)) continue;
        Global gl{g->name, g->type, g->init ? std::optional<ir::ConstexprValue>(*g->init) : std::nullopt};
        mod.globals.push_back(std::move(gl));
    }

    // translate functions
    for (auto& f : prog.funcs()) {
        mod.funcs.push_back(translate_func(*f, abi));
    }

    return mod;
}

}  // namespace rv64::isel
