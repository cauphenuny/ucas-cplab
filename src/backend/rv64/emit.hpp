#pragma once

#include "backend/ir/type.hpp"
#include "inst.hpp"
#include "utils/match.hpp"

#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

namespace rv64::emit {

// LI immediate decomposition: 32-bit signed
inline void emit_li(GeneralReg rd, int64_t imm64, std::vector<std::string>& out) {
    int32_t imm = (int32_t)imm64;
    if (imm >= -2048 && imm < 2048) {
        out.push_back(fmt::format("    addi {}, zero, {}", rd, imm));
        return;
    }
    // standard RV64 li decomposition
    int32_t hi20 = imm >> 12;    // sign-extended upper 20 bits
    int32_t lo12 = imm & 0xFFF;  // lower 12 bits
    if (lo12 & 0x800) {          // addi sign-extends; adjust lui
        hi20 += 1;
        lo12 -= 0x1000;
    }
    out.push_back(fmt::format("    lui {}, {}", rd, (uint32_t)hi20 & 0xFFFFF));
    if (lo12 != 0) {
        out.push_back(fmt::format("    addi {}, {}, {}", rd, rd, lo12));
    }
}

// Expand a single PseudoInst into real instruction strings
inline void expand_pseudo(const PseudoInst& pi, std::vector<std::string>& out) {
    switch (pi.op) {
        case Pseudo::LI: emit_li(pi.rd, pi.imm, out); break;
        case Pseudo::MV: out.push_back(fmt::format("    addi {}, {}, 0", pi.rd, pi.rs1)); break;
        case Pseudo::NOT: out.push_back(fmt::format("    xori {}, {}, -1", pi.rd, pi.rs1)); break;
        case Pseudo::NEG: out.push_back(fmt::format("    sub {}, zero, {}", pi.rd, pi.rs1)); break;
        case Pseudo::NEGW:
            out.push_back(fmt::format("    subw {}, zero, {}", pi.rd, pi.rs1));
            break;
        case Pseudo::SEQZ: out.push_back(fmt::format("    sltiu {}, {}, 1", pi.rd, pi.rs1)); break;
        case Pseudo::SNEZ:
            out.push_back(fmt::format("    sltu {}, zero, {}", pi.rd, pi.rs1));
            break;
        case Pseudo::BEQZ:
            out.push_back(fmt::format("    beq {}, zero, {}", pi.rd, pi.target));
            break;
        case Pseudo::BNEZ:
            out.push_back(fmt::format("    bne {}, zero, {}", pi.rd, pi.target));
            break;
        case Pseudo::J: out.push_back(fmt::format("    j {}", pi.target)); break;
        case Pseudo::CALL: out.push_back(fmt::format("    call {}", pi.target)); break;
        case Pseudo::RET: out.push_back("    ret"); break;
        case Pseudo::LA: out.push_back(fmt::format("    la {}, {}", pi.rd, pi.symbol)); break;
        case Pseudo::LOAD_GLOBAL: {
            out.push_back(fmt::format("    la t0, {}", pi.symbol));
            std::string load_op = (pi.elem_size == 8) ? "ld" : "lw";
            out.push_back(fmt::format("    {} {}, 0(t0)", load_op, pi.rd));
            break;
        }
        case Pseudo::STORE_GLOBAL: {
            out.push_back(fmt::format("    la t0, {}", pi.symbol));
            std::string store_op = (pi.elem_size == 8) ? "sd" : "sw";
            out.push_back(fmt::format("    {} {}, 0(t0)", store_op, pi.rs1));
            break;
        }
    }
}

// Emit a single instruction as a string
inline std::string emit_inst_str(const Inst& inst) {
    return Match{inst}(
        [](const InstR& i) { return fmt::format("    {} {}, {}, {}", i.op, i.rd, i.rs1, i.rs2); },
        [](const InstFR& i) { return fmt::format("    {} {}, {}, {}", i.op, i.rd, i.rs1, i.rs2); },
        [](const InstI& i) {
            bool is_load =
                (i.op == OpI::LD || i.op == OpI::LW || i.op == OpI::LWU || i.op == OpI::LB ||
                 i.op == OpI::LBU || i.op == OpI::LH || i.op == OpI::LHU);
            bool is_store =
                (i.op == OpI::SD || i.op == OpI::SW || i.op == OpI::SB || i.op == OpI::SH);
            if (is_load || is_store) {
                return fmt::format("    {} {}, {}({})", i.op, i.rd, i.imm, i.rs1);
            }
            return fmt::format("    {} {}, {}, {}", i.op, i.rd, i.rs1, i.imm);
        },
        [](const InstFI& i) { return fmt::format("    {} {}, {}({})", i.op, i.rd, i.imm, i.rs1); },
        [](const InstJ& i) { return fmt::format("    {} {}, {}", i.op, i.rd, i.target); },
        [](const InstU& i) { return fmt::format("    {} {}, {}", i.op, i.rd, i.imm); },
        [](const PseudoInst& pi) -> std::string {
            std::vector<std::string> lines;
            expand_pseudo(pi, lines);
            std::string result;
            for (size_t i = 0; i < lines.size(); i++) {
                if (i > 0) result += "\n";
                result += lines[i];
            }
            return result;
        });
}

// Emit a single global variable with its value
inline void emit_global_val(std::ostream& os, const Global& g, size_t elem_size,
                            const ir::Type& elem_type) {
    auto prim = elem_type.as<ir::type::Primitive>();
    auto& buffer = std::get<std::unique_ptr<std::byte[]>>(g.init->val);
    std::byte* ptr = buffer.get();
    size_t elem_count = g.type.flatten().as<ir::type::Array>().size;
    for (size_t i = 0; i < elem_count; i++) {
        Match{prim}(
            [&](ir::type::Int1) { os << fmt::format("    .byte {}\n", (int)*(bool*)ptr); },
            [&](ir::type::Int32) { os << fmt::format("    .word {}\n", *(int32_t*)ptr); },
            [&](ir::type::Int) { os << fmt::format("    .dword {}\n", *(int64_t*)ptr); },
            [&](ir::type::Float32) { os << fmt::format("    .word {}\n", *(int32_t*)ptr); },
            [&](ir::type::Float64) { os << fmt::format("    .dword {}\n", *(int64_t*)ptr); },
            [&](auto) { os << fmt::format("    .zero {}\n", elem_size); });
        ptr += elem_size;
    }
}

inline void emit(std::ostream& os, const Module& mod) {
    using namespace ir::type;
    os << ".option nopic\n";
    os << ".attribute arch, \"rv64i2p0_m2p0_a2p0_f2p0_d2p0_c2p0\"\n";
    os << ".attribute unaligned_access, 0\n";
    os << ".attribute stack_align, 16\n";

    // .rodata: const globals
    bool has_rodata = false;
    for (auto& g : mod.globals) {
        if (g.comptime) {
            has_rodata = true;
            break;
        }
    }
    if (has_rodata) {
        os << "\n.section .rodata\n";
        for (auto& g : mod.globals) {
            if (!g.comptime || g.is_zero_init()) continue;
            size_t sz = ir::type::size_of(g.type);
            os << ".align 2\n";
            os << fmt::format(".size {}, {}\n", g.name, sz);
            os << fmt::format(".globl {}\n", g.name);
            os << fmt::format("{}:\n", g.name);
            if (g.type.is<Array>()) {
                auto flat = g.type.flatten();
                emit_global_val(os, g, ir::type::size_of(flat.as<Array>().elem),
                                flat.as<Array>().elem);
            } else if (g.type.is<Primitive>()) {
                Match{g.init->val}(
                    [&](int v) { os << fmt::format("    .word {}\n", v); },
                    [&](int64_t v) { os << fmt::format("    .dword {}\n", v); },
                    [&](float v) { os << fmt::format("    .word {}\n", *(int32_t*)&v); },
                    [&](double v) { os << fmt::format("    .dword {}\n", *(int64_t*)&v); },
                    [&](bool v) { os << fmt::format("    .byte {}\n", v ? 1 : 0); },
                    [&](auto&&) {});
            }
        }
    }

    // .data: initialized non-const, non-zero globals
    bool has_data = false;
    for (auto& g : mod.globals) {
        if (!g.comptime && !g.is_zero_init()) {
            has_data = true;
            break;
        }
    }
    if (has_data) {
        os << "\n.data\n";
        for (auto& g : mod.globals) {
            if (g.comptime || g.is_zero_init()) continue;
            size_t sz = ir::type::size_of(g.type);
            os << ".align 2\n";
            os << fmt::format(".size {}, {}\n", g.name, sz);
            os << fmt::format(".globl {}\n", g.name);
            os << fmt::format("{}:\n", g.name);
            if (g.type.is<Array>()) {
                auto flat = g.type.flatten();
                emit_global_val(os, g, ir::type::size_of(flat.as<Array>().elem),
                                flat.as<Array>().elem);
            } else if (g.type.is<Primitive>()) {
                Match{g.init->val}(
                    [&](int v) { os << fmt::format("    .word {}\n", v); },
                    [&](int64_t v) { os << fmt::format("    .dword {}\n", v); },
                    [&](float v) { os << fmt::format("    .word {}\n", *(int32_t*)&v); },
                    [&](double v) { os << fmt::format("    .dword {}\n", *(int64_t*)&v); },
                    [&](bool v) { os << fmt::format("    .byte {}\n", v ? 1 : 0); },
                    [&](auto&&) {});
            }
        }
    }

    // .bss: zero-initialized non-const globals
    bool has_bss = false;
    for (auto& g : mod.globals) {
        if (!g.comptime && g.is_zero_init()) {
            has_bss = true;
            break;
        }
    }
    if (has_bss) {
        os << "\n.bss\n";
        for (auto& g : mod.globals) {
            if (g.comptime || !g.is_zero_init()) continue;
            size_t sz = ir::type::size_of(g.type);
            os << ".align 2\n";
            os << fmt::format(".lcomm {}, {}\n", g.name, sz);
        }
    }

    // .text
    os << "\n.text\n";
    os << ".align 1\n";
    for (auto& f : mod.funcs) {
        os << fmt::format(".globl {}\n", f.name);
        os << fmt::format(".type {}, @function\n", f.name);
        bool seen_entry_label = false;
        for (auto& b : f.blocks) {
            if (!seen_entry_label && b.label == f.name) {
                os << fmt::format("{}:\n", f.name);
                seen_entry_label = true;
            } else {
                os << fmt::format("{}:\n", b.label);
            }
            for (auto& inst : b.insts) {
                os << emit_inst_str(inst) << "\n";
            }
        }
        os << fmt::format(".size {}, .-{}\n", f.name, f.name) << "\n";
    }

    // .rodata for float literals
    if (!mod.float_literals.empty()) {
        os << ".section .rodata\n";
        for (auto& lit : mod.float_literals) {
            os << ".balign 8\n";
            os << fmt::format("{}:\n", lit.label);
            Match{lit.value.val}(
                [&](float v) { os << fmt::format("    .word {}\n", *(int32_t*)&v); },
                [&](double v) { os << fmt::format("    .dword {}\n", *(int64_t*)&v); },
                [&](auto&&) {});
        }
    }

    os << ".ident \"CACT compiler\"\n";
}

}  // namespace rv64::emit
