#include "backend/ir/ir.h"
#include "backend/ir/type.hpp"
#include "backend/rv64/inst.hpp"
#include "backend/rv64/op.hpp"
#include "utils/match.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace rv64 {

// ============ Instruction toString() implementations ============

std::string InstR::toString() const {
    return fmt::format("    {} {}, {}, {}", op, rd, rs1, rs2);
}

std::string InstFR::toString() const {
    return Match{rs2}(
        [&](const std::monostate&) { return fmt::format("    {} {}, {}", op, rd, rs1); },
        [&](const auto& rs2) { return fmt::format("    {} {}, {}, {}", op, rd, rs1, rs2); });
}

std::string InstI::toString() const {
    bool is_load = (op == OpI::LD || op == OpI::LW || op == OpI::LWU || op == OpI::LB ||
                    op == OpI::LBU || op == OpI::LH || op == OpI::LHU);
    bool is_store = (op == OpI::SD || op == OpI::SW || op == OpI::SB || op == OpI::SH);
    if (is_load || is_store) {
        return fmt::format("    {} {}, {}({})", op, rd, imm, rs1);
    }
    return fmt::format("    {} {}, {}, {}", op, rd, rs1, imm);
}

std::string InstFI::toString() const {
    return fmt::format("    {} {}, {}({})", op, rd, imm, rs1);
}

std::string InstJ::toString() const {
    return fmt::format("    {} {}, {}", op, rd, target);
}

std::string InstU::toString() const {
    return fmt::format("    {} {}, {}", op, rd, imm);
}

// ============ Pseudo instruction toString() implementations ============

std::string PseudoR::toString() const {
    std::string op_str;
    switch (op) {
        case MV: op_str = "mv"; break;
        case NOT: op_str = "not"; break;
        case NEG: op_str = "neg"; break;
        case NEGW: op_str = "negw"; break;
        case SEQZ: op_str = "seqz"; break;
        case SNEZ: op_str = "snez"; break;
    }
    return fmt::format("    {} {}, {}", op_str, rd, rs1);
}

std::string PseudoLI::toString() const {
    return fmt::format("    li {}, {}", rd, imm);
}

std::string PseudoB::toString() const {
    switch (op) {
        case BEQZ: return fmt::format("    beqz {}, {}", rs1, target);
        case BNEZ: return fmt::format("    bnez {}, {}", rs1, target);
    }
    return "";
}

std::string PseudoJ::toString() const {
    switch (op) {
        case J: return fmt::format("    j {}", target);
        case CALL: return fmt::format("    call {}", target);
    }
    return "";
}

std::string PseudoL::toString() const {
    switch (op) {
        case LA: return fmt::format("    la {}, {}", rd, symbol);
        case LGD: return fmt::format("    la t0, {}\n    ld {}, 0(t0)", symbol, rd);
        case LGW: return fmt::format("    la t0, {}\n    lw {}, 0(t0)", symbol, rd);
        case SGD: return fmt::format("    la t0, {}\n    sd {}, 0(t0)", symbol, rd);
        case SGW: return fmt::format("    la t0, {}\n    sw {}, 0(t0)", symbol, rd);
    }
    return "";
}

std::string PseudoRet::toString() const {
    return "    ret";
}

// ============ AsmBlock, AsmFunc, FloatLiteral toString() ============

std::string AsmBlock::toString() const {
    std::string r;
    if (!label.empty()) {
        r += fmt::format("{}:\n", label);
    }
    for (auto& inst : insts) {
        r += fmt::format("{}\n", inst);
    }
    return r;
}

std::string AsmFunc::toString() const {
    std::string r;
    r += fmt::format(".globl {}\n", name);
    r += fmt::format(".type {}, @function\n", name);
    for (auto& b : blocks) {
        r += fmt::format("{}", b);
    }
    r += fmt::format(".size {}, .-{}\n", name, name);
    return r;
}

std::string FloatLiteral::toString() const {
    std::string r;
    r += ".balign 8\n";
    r += fmt::format("{}:\n", label);
    Match{value.val}([&](float v) { r += fmt::format("    .word {}\n", *(int32_t*)&v); },
                     [&](double v) { r += fmt::format("    .dword {}\n", *(int64_t*)&v); },
                     [&](auto&&) {});
    return r;
}

// ============ Global::toString() ============

namespace {

void append_global_val(std::string& r, const Global& g, size_t elem_size,
                       const ir::type::TypeBox& elem_type) {
    auto prim = elem_type.as<ir::type::Primitive>();
    auto& buffer = std::get<std::unique_ptr<std::byte[]>>(g.init->val);
    std::byte* ptr = buffer.get();
    size_t elem_count = g.type.flatten().as<ir::type::Array>().size;
    for (size_t i = 0; i < elem_count; i++) {
        Match{prim}([&](ir::type::Int1) { r += fmt::format("    .byte {}\n", (int)*(bool*)ptr); },
                    [&](ir::type::Int32) { r += fmt::format("    .word {}\n", *(int32_t*)ptr); },
                    [&](ir::type::Int) { r += fmt::format("    .dword {}\n", *(int64_t*)ptr); },
                    [&](ir::type::Float32) { r += fmt::format("    .word {}\n", *(int32_t*)ptr); },
                    [&](ir::type::Float64) { r += fmt::format("    .dword {}\n", *(int64_t*)ptr); },
                    [&](auto) { r += fmt::format("    .zero {}\n", elem_size); });
        ptr += elem_size;
    }
}

}  // namespace

std::string Global::toString() const {
    size_t sz = ir::type::size_of(type);
    std::string r;
    r += ".align 2\n";
    r += fmt::format(".size {}, {}\n", name, sz);
    r += fmt::format(".globl {}\n", name);
    r += fmt::format("{}:\n", name);
    if (type.is<ir::type::Array>()) {
        auto flat = type.flatten();
        append_global_val(r, *this, ir::type::size_of(flat.as<ir::type::Array>().elem),
                          flat.as<ir::type::Array>().elem);
    } else if (type.is<ir::type::Primitive>()) {
        Match{init->val}([&](int v) { r += fmt::format("    .word {}\n", v); },
                         [&](int64_t v) { r += fmt::format("    .dword {}\n", v); },
                         [&](float v) { r += fmt::format("    .word {}\n", *(int32_t*)&v); },
                         [&](double v) { r += fmt::format("    .dword {}\n", *(int64_t*)&v); },
                         [&](bool v) { r += fmt::format("    .byte {}\n", v ? 1 : 0); },
                         [&](auto&&) {});
    }
    return r;
}

// ============ Module::toString() ============

std::string Module::toString() const {
    std::string r;
    r += ".option nopic\n";
    r += ".attribute arch, \"rv64i2p0_m2p0_a2p0_f2p0_d2p0_c2p0\"\n";
    r += ".attribute unaligned_access, 0\n";
    r += ".attribute stack_align, 16\n";

    // .rodata: const (comptime) globals
    bool has_rodata = false;
    for (auto& g : globals) {
        if (g.comptime) {
            has_rodata = true;
            break;
        }
    }
    if (has_rodata) {
        r += "\n.section .rodata\n";
        for (auto& g : globals) {
            if (!g.comptime || g.is_zero_init()) continue;
            r += fmt::format("{}", g);
        }
    }

    // .data: initialized non-const, non-zero globals
    bool has_data = false;
    for (auto& g : globals) {
        if (!g.comptime && !g.is_zero_init()) {
            has_data = true;
            break;
        }
    }
    if (has_data) {
        r += "\n.data\n";
        for (auto& g : globals) {
            if (g.comptime || g.is_zero_init()) continue;
            r += fmt::format("{}", g);
        }
    }

    // .bss: zero-initialized non-const globals
    bool has_bss = false;
    for (auto& g : globals) {
        if (!g.comptime && g.is_zero_init()) {
            has_bss = true;
            break;
        }
    }
    if (has_bss) {
        r += "\n.bss\n";
        for (auto& g : globals) {
            if (g.comptime || !g.is_zero_init()) continue;
            size_t sz = ir::type::size_of(g.type);
            r += ".align 2\n";
            r += fmt::format(".lcomm {}, {}\n", g.name, sz);
        }
    }

    // .text
    r += "\n.text\n";
    r += ".align 1\n";
    for (auto& f : funcs) {
        r += fmt::format("{}", f);
        r += "\n";
    }

    // .rodata for float literals
    if (!float_literals.empty()) {
        r += ".section .rodata\n";
        for (auto& lit : float_literals) {
            r += fmt::format("{}", lit);
        }
    }

    r += ".ident \"CACT compiler\"\n";
    return r;
}

}  // namespace rv64
