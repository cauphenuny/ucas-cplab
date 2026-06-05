#pragma once

#include "abi.hpp"
#include "inst.hpp"
#include "module.hpp"
#include "backend/ir/type.hpp"
#include "utils/match.hpp"

#include <cstdint>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace rv64::emit {

// opcode name strings
inline std::string op_name(OpR op) {
    switch (op) {
        case OpR::ADD: return "add"; case OpR::SUB: return "sub"; case OpR::SLL: return "sll";
        case OpR::SLT: return "slt"; case OpR::SLTU: return "sltu"; case OpR::XOR: return "xor";
        case OpR::SRL: return "srl"; case OpR::SRA: return "sra"; case OpR::OR: return "or";
        case OpR::AND: return "and"; case OpR::MUL: return "mul"; case OpR::DIV: return "div";
        case OpR::REM: return "rem";
    }
    return "?";
}

inline std::string op_name(OpFR op) {
    switch (op) {
        case OpFR::FADD_S: return "fadd.s"; case OpFR::FADD_D: return "fadd.d";
        case OpFR::FSUB_S: return "fsub.s"; case OpFR::FSUB_D: return "fsub.d";
        case OpFR::FMUL_S: return "fmul.s"; case OpFR::FMUL_D: return "fmul.d";
        case OpFR::FDIV_S: return "fdiv.s"; case OpFR::FDIV_D: return "fdiv.d";
        case OpFR::FSQRT_S: return "fsqrt.s"; case OpFR::FSQRT_D: return "fsqrt.d";
        case OpFR::FSGNJ_S: return "fsgnj.s"; case OpFR::FSGNJ_D: return "fsgnj.d";
        case OpFR::FSGNJN_S: return "fsgnjn.s"; case OpFR::FSGNJN_D: return "fsgnjn.d";
        case OpFR::FEQ_S: return "feq.s"; case OpFR::FEQ_D: return "feq.d";
        case OpFR::FLT_S: return "flt.s"; case OpFR::FLT_D: return "flt.d";
        case OpFR::FLE_S: return "fle.s"; case OpFR::FLE_D: return "fle.d";
        case OpFR::FCVT_W_S: return "fcvt.w.s"; case OpFR::FCVT_W_D: return "fcvt.w.d";
        case OpFR::FCVT_L_S: return "fcvt.l.s"; case OpFR::FCVT_L_D: return "fcvt.l.d";
        case OpFR::FCVT_S_W: return "fcvt.s.w"; case OpFR::FCVT_D_W: return "fcvt.d.w";
        case OpFR::FCVT_S_L: return "fcvt.s.l"; case OpFR::FCVT_D_L: return "fcvt.d.l";
        case OpFR::FCVT_S_D: return "fcvt.s.d"; case OpFR::FCVT_D_S: return "fcvt.d.s";
        case OpFR::FMV_X_W: return "fmv.x.w"; case OpFR::FMV_W_X: return "fmv.w.x";
    }
    return "?";
}

inline std::string op_name(OpI op) {
    switch (op) {
        case OpI::ADDI: return "addi"; case OpI::SLLI: return "slli";
        case OpI::SLTI: return "slti"; case OpI::SLTIU: return "sltiu";
        case OpI::XORI: return "xori"; case OpI::SRLI: return "srli";
        case OpI::SRAI: return "srai"; case OpI::ORI: return "ori";
        case OpI::ANDI: return "andi";
        case OpI::LD: return "ld"; case OpI::LW: return "lw"; case OpI::LWU: return "lwu";
        case OpI::LB: return "lb"; case OpI::LBU: return "lbu";
        case OpI::LH: return "lh"; case OpI::LHU: return "lhu";
        case OpI::JALR: return "jalr";
        case OpI::SD: return "sd"; case OpI::SW: return "sw";
        case OpI::SB: return "sb"; case OpI::SH: return "sh";
        case OpI::BEQ: return "beq"; case OpI::BNE: return "bne";
        case OpI::BLT: return "blt"; case OpI::BGE: return "bge";
        case OpI::BLTU: return "bltu"; case OpI::BGEU: return "bgeu";
        case OpI::ADDIW: return "addiw"; case OpI::SLLIW: return "slliw";
        case OpI::SRLIW: return "srliw"; case OpI::SRAIW: return "sraiw";
        case OpI::ADDW: return "addw"; case OpI::SUBW: return "subw";
        case OpI::SLLW: return "sllw"; case OpI::SRLW: return "srlw";
        case OpI::SRAW: return "sraw"; case OpI::MULW: return "mulw";
        case OpI::DIVW: return "divw"; case OpI::REMW: return "remw";
    }
    return "?";
}

inline std::string op_name(OpFI op) {
    switch (op) {
        case OpFI::FLW: return "flw"; case OpFI::FLD: return "fld";
        case OpFI::FSW: return "fsw"; case OpFI::FSD: return "fsd";
    }
    return "?";
}

inline std::string op_name(OpJ op) {
    switch (op) {
        case OpJ::JAL: return "jal";
    }
    return "?";
}

inline std::string op_name(OpU op) {
    switch (op) {
        case OpU::LUI: return "lui"; case OpU::AUIPC: return "auipc";
    }
    return "?";
}

// LI immediate decomposition: 32-bit signed
inline void emit_li(GeneralReg rd, int64_t imm64, std::vector<std::string>& out) {
    int32_t imm = (int32_t)imm64;
    // 12-bit immediate range
    if (imm >= -2048 && imm < 2048) {
        out.push_back("    addi " + rd.toString() + ", zero, " + std::to_string(imm));
        return;
    }
    // lui + addi with sign extension fix
    int32_t hi = (imm + 0x800) >> 12;  // hi20 with adjustment
    int32_t lo = imm - (hi << 12);
    out.push_back("    lui " + rd.toString() + ", " + std::to_string(hi));
    if (lo != 0) {
        out.push_back("    addi " + rd.toString() + ", " + rd.toString() + ", " + std::to_string(lo));
    }
}

// Expand a single PseudoInst into real instruction strings
inline void expand_pseudo(const PseudoInst& pi, std::vector<std::string>& out) {
    switch (pi.op) {
        case Pseudo::LI:
            emit_li(pi.rd, pi.imm, out);
            break;
        case Pseudo::MV:
            out.push_back("    addi " + pi.rd.toString() + ", " + pi.rs1.toString() + ", 0");
            break;
        case Pseudo::NOT:
            out.push_back("    xori " + pi.rd.toString() + ", " + pi.rs1.toString() + ", -1");
            break;
        case Pseudo::NEG:
            out.push_back("    sub " + pi.rd.toString() + ", zero, " + pi.rs1.toString());
            break;
        case Pseudo::NEGW:
            out.push_back("    subw " + pi.rd.toString() + ", zero, " + pi.rs1.toString());
            break;
        case Pseudo::SEQZ:
            out.push_back("    sltiu " + pi.rd.toString() + ", " + pi.rs1.toString() + ", 1");
            break;
        case Pseudo::SNEZ:
            out.push_back("    sltu " + pi.rd.toString() + ", zero, " + pi.rs1.toString());
            break;
        case Pseudo::BEQZ:
            out.push_back("    beq " + pi.rd.toString() + ", zero, " + pi.target);
            break;
        case Pseudo::BNEZ:
            out.push_back("    bne " + pi.rd.toString() + ", zero, " + pi.target);
            break;
        case Pseudo::J:
            out.push_back("    j " + pi.target);
            break;
        case Pseudo::CALL:
            out.push_back("    call " + pi.target);
            break;
        case Pseudo::RET:
            out.push_back("    ret");
            break;
        case Pseudo::LA:
            out.push_back("    la " + pi.rd.toString() + ", " + pi.symbol);
            break;
        case Pseudo::LOAD_GLOBAL: {
            out.push_back("    la t0, " + pi.symbol);
            std::string load_op = (pi.elem_size == 8) ? "ld" : "lw";
            out.push_back("    " + load_op + " " + pi.rd.toString() + ", 0(t0)");
            break;
        }
        case Pseudo::STORE_GLOBAL: {
            out.push_back("    la t0, " + pi.symbol);
            std::string store_op = (pi.elem_size == 8) ? "sd" : "sw";
            out.push_back("    " + store_op + " " + pi.rs1.toString() + ", 0(t0)");
            break;
        }
    }
}

// Emit a single instruction as a string
inline std::string emit_inst_str(const Inst& inst) {
    return Match{inst}(
        [](const InstR& i) {
            return "    " + op_name(i.op) + " " + i.rd.toString() + ", " + i.rs1.toString() +
                   ", " + i.rs2.toString();
        },
        [](const InstFR& i) {
            return "    " + op_name(i.op) + " " + i.rd.toString() + ", " + i.rs1.toString() +
                   ", " + i.rs2.toString();
        },
        [](const InstI& i) {
            bool is_load = (i.op == OpI::LD || i.op == OpI::LW || i.op == OpI::LWU ||
                            i.op == OpI::LB || i.op == OpI::LBU || i.op == OpI::LH ||
                            i.op == OpI::LHU);
            bool is_store = (i.op == OpI::SD || i.op == OpI::SW || i.op == OpI::SB ||
                             i.op == OpI::SH);
            if (is_load || is_store) {
                return "    " + op_name(i.op) + " " + i.rd.toString() + ", " +
                       std::to_string(i.imm) + "(" + i.rs1.toString() + ")";
            }
            return "    " + op_name(i.op) + " " + i.rd.toString() + ", " + i.rs1.toString() +
                   ", " + std::to_string(i.imm);
        },
        [](const InstFI& i) {
            return "    " + op_name(i.op) + " " + i.rd.toString() + ", " +
                   std::to_string(i.imm) + "(" + i.rs1.toString() + ")";
        },
        [](const InstJ& i) {
            return "    " + op_name(i.op) + " " + i.rd.toString() + ", " + i.target;
        },
        [](const InstU& i) {
            return "    " + op_name(i.op) + " " + i.rd.toString() + ", " +
                   std::to_string(i.imm);
        },
        [](const PseudoInst& pi) -> std::string {
            std::vector<std::string> lines;
            expand_pseudo(pi, lines);
            // join lines with newline
            std::string result;
            for (size_t i = 0; i < lines.size(); i++) {
                if (i > 0) result += "\n";
                result += lines[i];
            }
            return result;
        });
}

inline void emit(std::ostream& os, const Module& mod) {
    // .data section
    if (!mod.globals.empty()) {
        os << ".section .data\n";
        for (auto& g : mod.globals) {
            auto flat = g.type.flatten();
            size_t total_size = ir::type::size_of(g.type);
            if (g.init) {
                using namespace ir::type;
                if (g.type.is<Array>()) {
                    size_t elem_count = flat.as<Array>().size;
                    auto elem = flat.as<Array>().elem;
                    size_t elem_size = ir::type::size_of(elem);
                    if (elem.is<Primitive>()) {
                        auto prim = elem.as<Primitive>();
                        os << ".balign " << elem_size << "\n";
                        os << ".globl " << g.name << "\n";
                        os << g.name << ":\n";
                        auto& buffer = std::get<std::unique_ptr<std::byte[]>>(g.init->val);
                        std::byte* ptr = buffer.get();
                        for (size_t i = 0; i < elem_count; i++) {
                            Match{prim}(
                                [&](ir::type::Int1) { os << "    .byte " << (int)*(bool*)ptr << "\n"; },
                                [&](ir::type::Int32) { os << "    .word " << *(int32_t*)ptr << "\n"; },
                                [&](ir::type::Int) { os << "    .dword " << *(int64_t*)ptr << "\n"; },
                                [&](ir::type::Float32) { os << "    .word " << *(int32_t*)ptr << "\n"; },
                                [&](ir::type::Float64) { os << "    .dword " << *(int64_t*)ptr << "\n"; },
                                [&](auto) { os << "    .zero " << elem_size << "\n"; });
                            ptr += elem_size;
                        }
                    }
                } else if (g.type.is<Primitive>()) {
                    size_t sz = ir::type::size_of(g.type);
                    os << ".balign " << sz << "\n";
                    os << ".globl " << g.name << "\n";
                    os << g.name << ":\n";
                    // extract init value
                    Match{g.init->val}(
                        [&](int v) { os << "    .word " << v << "\n"; },
                        [&](int64_t v) { os << "    .dword " << v << "\n"; },
                        [&](float v) { os << "    .word " << *(int32_t*)&v << "\n"; },
                        [&](double v) { os << "    .dword " << *(int64_t*)&v << "\n"; },
                        [&](bool v) { os << "    .byte " << (v ? 1 : 0) << "\n"; },
                        [&](auto&&) { os << "    .zero " << sz << "\n"; });
                }
            } else {
                os << ".balign " << ir::type::size_of(g.type) << "\n";
                os << ".comm " << g.name << ", " << total_size << ", " << total_size << "\n";
            }
        }
    }

    // .text section
    os << ".section .text\n";
    for (auto& f : mod.funcs) {
        os << ".balign 4\n";
        os << ".globl " << f.name << "\n";
        bool seen_entry_label = false;  // function name IS the entry label
        for (auto& b : f.blocks) {
            // first block's label is the function name itself
            if (!seen_entry_label && b.label == f.name) {
                os << f.name << ":\n";
                seen_entry_label = true;
            } else {
                os << b.label << ":\n";
            }
            for (auto& inst : b.insts) {
                os << emit_inst_str(inst) << "\n";
            }
        }
        os << "\n";
    }

    // .rodata for float literals
    if (!mod.float_literals.empty()) {
        os << ".section .rodata\n";
        for (auto& lit : mod.float_literals) {
            os << ".balign 8\n";
            os << lit.label << ":\n";
            Match{lit.value.val}(
                [&](float v) { os << "    .word " << *(int32_t*)&v << "\n"; },
                [&](double v) { os << "    .dword " << *(int64_t*)&v << "\n"; },
                [&](auto&&) {});
        }
    }
}

}  // namespace rv64::emit
