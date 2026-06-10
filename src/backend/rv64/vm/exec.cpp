#include "backend/rv64/abi.hpp"
#include "backend/rv64/vm/vm.hpp"
#include "fmt/base.h"
#include "utils/match.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <variant>

namespace rv64::vm {

// ---------------------------------------------------------------------------
// Builtin function implementations
// ---------------------------------------------------------------------------

namespace {

void builtin_print_int(VirtualMachine& vm) {
    int64_t val = (int64_t)vm.regs[10];
    vm.output << val << '\n';
}

void builtin_print_float(VirtualMachine& vm) {
    float val = vm.get_f(10);
    vm.output << fmt::format("{:.6f}\n", val);
}

void builtin_print_double(VirtualMachine& vm) {
    double val = vm.get_d(10);
    vm.output << fmt::format("{:.6f}\n", val);
}

void builtin_print_bool(VirtualMachine& vm) {
    vm.output << (vm.regs[10] ? "true" : "false") << '\n';
}

void builtin_get_int(VirtualMachine& vm) {
    int val;
    vm.input >> val;
    vm.regs[10] = (int64_t)val;
}

void builtin_get_float(VirtualMachine& vm) {
    float val;
    vm.input >> val;
    vm.set_f(10, val);
}

void builtin_get_double(VirtualMachine& vm) {
    double val;
    vm.input >> val;
    vm.set_d(10, val);
}

}  // namespace

// ---------------------------------------------------------------------------
// Initialization helpers
// ---------------------------------------------------------------------------

void VirtualMachine::init_builtins() {
    builtins["print_int"] = builtin_print_int;
    builtins["print_float"] = builtin_print_float;
    builtins["print_double"] = builtin_print_double;
    builtins["print_bool"] = builtin_print_bool;
    builtins["get_int"] = builtin_get_int;
    builtins["get_float"] = builtin_get_float;
    builtins["get_double"] = builtin_get_double;
}

void VirtualMachine::build_flat_insts(const Module& mod) {
    size_t idx = 0;
    for (auto& func : mod.funcs) {
        for (auto& block : func.blocks) {
            label_map[block.label] = idx;
            for (auto& inst : block.insts) {
                flat_insts.push_back({inst, false, 0});
                idx++;
            }
        }
    }
}

void VirtualMachine::place_globals(const Module& mod) {
    uint64_t offset = GLOBAL_BASE;
    for (auto& g : mod.globals) {
        size_t align = abi::size(g.type);
        if (align > 1) [[likely]] {
            offset = (offset + align - 1) & ~(align - 1);
        }
        symbol_map[g.name] = offset;
        size_t sz = abi::size(g.type);
        if (g.init) {
            Match{g.init->val}([&](std::monostate) { /* zero — already zeroed from resize() */ },
                               [&](int v) { store<int>(offset, v); },
                               [&](int64_t v) { store<int64_t>(offset, v); },
                               [&](float v) { store<float>(offset, v); },
                               [&](bool v) { store<uint8_t>(offset, v ? 1 : 0); },
                               [&](double v) { store<double>(offset, v); },
                               [&](const std::unique_ptr<std::byte[]>& buf) {
                                   std::memcpy(&memory[offset], buf.get(), sz);
                               });
        }
        offset += sz;
    }
}

void VirtualMachine::resolve_branches() {
    for (auto& fi : flat_insts) {
        Match{fi.inst}(
            [&](const PseudoB& i) {
                auto it = label_map.find(i.target);
                if (it != label_map.end()) fi.branch_target = it->second;
            },
            [&](const PseudoJ& i) {
                if (i.op == PseudoJ::CALL) {
                    auto bit = builtins.find(i.target);
                    if (bit != builtins.end()) {
                        fi.is_call_builtin = true;
                        return;
                    }
                }
                auto it = label_map.find(i.target);
                if (it != label_map.end()) fi.branch_target = it->second;
            },
            [&](const InstJ& i) {
                auto it = label_map.find(i.target);
                if (it != label_map.end()) fi.branch_target = it->second;
            },
            [&](const auto&) { /* no branch target */ });
    }
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

uint8_t VirtualMachine::execute(const Module& mod) {
    // Reset state
    regs.fill(0);
    fregs.fill(0);
    memory.clear();
    flat_insts.clear();
    label_map.clear();
    symbol_map.clear();
    call_stack.clear();
    num_insts = 0;

    memory.resize(MEM_SIZE, 0);

    // Build execution structures
    build_flat_insts(mod);
    place_globals(mod);
    resolve_branches();

    // Set up initial register state
    regs[2] = STACK_INIT;  // sp
    regs[1] = ~0ULL;       // ra sentinel — exec_pseudo_ret exits when ra is sentinel

    // Find main and run
    auto main_it = label_map.find("main");
    if (main_it == label_map.end()) {
        throw COMPILER_ERROR("RV64 VM: no 'main' function found");
    }
    pc = main_it->second;

    // Execution loop
    while (pc < flat_insts.size()) {
        if (num_insts >= max_insts) [[unlikely]] {
            throw COMPILER_ERROR(fmt::format("RV64 VM: execution limit ({}) exceeded", max_insts));
        }
        auto& fi = flat_insts[pc];
        pc++;
        exec(fi.inst, fi);
        num_insts++;
    }

    // Return value in a0
    return (uint8_t)(regs[10] & 0xFF);
}

// ---------------------------------------------------------------------------
// Top-level instruction dispatch
// ---------------------------------------------------------------------------

void VirtualMachine::exec(const Inst& inst, const FlatInst& fi) {
    match(
        inst, [&](const InstR& i) { exec_r(i); }, [&](const InstFR& i) { exec_fr(i); },
        [&](const InstI& i) { exec_i(i, fi); }, [&](const InstFI& i) { exec_fi(i); },
        [&](const InstJ& i) { exec_j(i, fi); }, [&](const InstU& i) { exec_u(i); },
        [&](const PseudoR& i) { exec_pseudo_r(i); }, [&](const PseudoLI& i) { exec_pseudo_li(i); },
        [&](const PseudoB& i) { exec_pseudo_b(i, fi); },
        [&](const PseudoJ& i) { exec_pseudo_j(i, fi); },
        [&](const PseudoL& i) { exec_pseudo_l(i); }, [&](const PseudoRet&) { exec_pseudo_ret(); });
}

// ---------------------------------------------------------------------------
// Real RISC-V instruction implementations
// ---------------------------------------------------------------------------

void VirtualMachine::exec_r(const InstR& i) {
    using R = OpR;
    switch (i.op) {
        case R::ADD: regs[i.rd.id] = (int64_t)regs[i.rs1.id] + (int64_t)regs[i.rs2.id]; break;
        case R::SUB: regs[i.rd.id] = (int64_t)regs[i.rs1.id] - (int64_t)regs[i.rs2.id]; break;
        case R::SLL: regs[i.rd.id] = regs[i.rs1.id] << (regs[i.rs2.id] & 0x3F); break;
        case R::SLT: regs[i.rd.id] = (int64_t)regs[i.rs1.id] < (int64_t)regs[i.rs2.id]; break;
        case R::SLTU: regs[i.rd.id] = regs[i.rs1.id] < regs[i.rs2.id]; break;
        case R::XOR: regs[i.rd.id] = regs[i.rs1.id] ^ regs[i.rs2.id]; break;
        case R::SRL: regs[i.rd.id] = regs[i.rs1.id] >> (regs[i.rs2.id] & 0x3F); break;
        case R::SRA:
            regs[i.rd.id] = (uint64_t)((int64_t)regs[i.rs1.id] >> (regs[i.rs2.id] & 0x3F));
            break;
        case R::OR: regs[i.rd.id] = regs[i.rs1.id] | regs[i.rs2.id]; break;
        case R::AND: regs[i.rd.id] = regs[i.rs1.id] & regs[i.rs2.id]; break;
        case R::MUL: regs[i.rd.id] = (int64_t)regs[i.rs1.id] * (int64_t)regs[i.rs2.id]; break;
        case R::DIV: {
            if (regs[i.rs2.id] == 0) {
                regs[i.rd.id] = ~0ULL;
            } else if ((int64_t)regs[i.rs1.id] == INT64_MIN && (int64_t)regs[i.rs2.id] == -1) {
                regs[i.rd.id] = (uint64_t)INT64_MIN;
            } else {
                regs[i.rd.id] = (int64_t)regs[i.rs1.id] / (int64_t)regs[i.rs2.id];
            }
            break;
        }
        case R::REM: {
            if (regs[i.rs2.id] == 0) {
                regs[i.rd.id] = regs[i.rs1.id];
            } else if ((int64_t)regs[i.rs1.id] == INT64_MIN && (int64_t)regs[i.rs2.id] == -1) {
                regs[i.rd.id] = 0;
            } else {
                regs[i.rd.id] = (int64_t)regs[i.rs1.id] % (int64_t)regs[i.rs2.id];
            }
            break;
        }
    }
}

void VirtualMachine::exec_fr(const InstFR& i) {
    using FR = OpFR;
    switch (i.op) {
        // Float arithmetic (single precision)
        case FR::FADD_S: set_f(i.rd.id, get_f(i.rs1.id) + get_f(i.rs2.id)); break;
        case FR::FSUB_S: set_f(i.rd.id, get_f(i.rs1.id) - get_f(i.rs2.id)); break;
        case FR::FMUL_S: set_f(i.rd.id, get_f(i.rs1.id) * get_f(i.rs2.id)); break;
        case FR::FDIV_S: set_f(i.rd.id, get_f(i.rs1.id) / get_f(i.rs2.id)); break;
        case FR::FSQRT_S: set_f(i.rd.id, __builtin_sqrtf(get_f(i.rs1.id))); break;

        // Double arithmetic (double precision)
        case FR::FADD_D: set_d(i.rd.id, get_d(i.rs1.id) + get_d(i.rs2.id)); break;
        case FR::FSUB_D: set_d(i.rd.id, get_d(i.rs1.id) - get_d(i.rs2.id)); break;
        case FR::FMUL_D: set_d(i.rd.id, get_d(i.rs1.id) * get_d(i.rs2.id)); break;
        case FR::FDIV_D: set_d(i.rd.id, get_d(i.rs1.id) / get_d(i.rs2.id)); break;
        case FR::FSQRT_D: set_d(i.rd.id, __builtin_sqrt(get_d(i.rs1.id))); break;

        // Sign-injection (used for MOV/NEG of floats)
        case FR::FSGNJ_S: {
            uint32_t v1, v2;
            std::memcpy(&v1, &fregs[i.rs1.id], 4);
            std::memcpy(&v2, &fregs[i.rs2.id], 4);
            v1 = (v1 & 0x7FFFFFFF) | (v2 & 0x80000000);
            std::memcpy(&fregs[i.rd.id], &v1, 4);
            break;
        }
        case FR::FSGNJN_S: {
            uint32_t v1, v2;
            std::memcpy(&v1, &fregs[i.rs1.id], 4);
            std::memcpy(&v2, &fregs[i.rs2.id], 4);
            v1 = (v1 & 0x7FFFFFFF) | ((v2 & 0x80000000) ^ 0x80000000);
            std::memcpy(&fregs[i.rd.id], &v1, 4);
            break;
        }
        case FR::FSGNJ_D: {
            uint64_t v1, v2;
            std::memcpy(&v1, &fregs[i.rs1.id], 8);
            std::memcpy(&v2, &fregs[i.rs2.id], 8);
            v1 = (v1 & 0x7FFFFFFFFFFFFFFFULL) | (v2 & 0x8000000000000000ULL);
            std::memcpy(&fregs[i.rd.id], &v1, 8);
            break;
        }
        case FR::FSGNJN_D: {
            uint64_t v1, v2;
            std::memcpy(&v1, &fregs[i.rs1.id], 8);
            std::memcpy(&v2, &fregs[i.rs2.id], 8);
            v1 = (v1 & 0x7FFFFFFFFFFFFFFFULL) |
                 ((v2 & 0x8000000000000000ULL) ^ 0x8000000000000000ULL);
            std::memcpy(&fregs[i.rd.id], &v1, 8);
            break;
        }

        // Float compares (produce 0/1 in integer register via fregs[rd])
        case FR::FEQ_S: regs[i.rd.id] = get_f(i.rs1.id) == get_f(i.rs2.id); break;
        case FR::FLT_S: regs[i.rd.id] = get_f(i.rs1.id) < get_f(i.rs2.id); break;
        case FR::FLE_S: regs[i.rd.id] = get_f(i.rs1.id) <= get_f(i.rs2.id); break;
        case FR::FEQ_D: regs[i.rd.id] = get_d(i.rs1.id) == get_d(i.rs2.id); break;
        case FR::FLT_D: regs[i.rd.id] = get_d(i.rs1.id) < get_d(i.rs2.id); break;
        case FR::FLE_D: regs[i.rd.id] = get_d(i.rs1.id) <= get_d(i.rs2.id); break;

        // FCVT: int ↔ float (note: rd=GPR, rs1=FPR or vice versa)
        case FR::FCVT_W_S: regs[i.rd.id] = (int64_t)(int32_t)get_f(i.rs1.id); break;
        case FR::FCVT_W_D: regs[i.rd.id] = (int64_t)(int32_t)get_d(i.rs1.id); break;
        case FR::FCVT_L_S: regs[i.rd.id] = (int64_t)get_f(i.rs1.id); break;
        case FR::FCVT_L_D: regs[i.rd.id] = (int64_t)get_d(i.rs1.id); break;
        case FR::FCVT_S_W: set_f(i.rd.id, (float)(int32_t)regs[i.rs1.id]); break;
        case FR::FCVT_D_W: set_d(i.rd.id, (double)(int32_t)regs[i.rs1.id]); break;
        case FR::FCVT_S_L: set_f(i.rd.id, (float)(int64_t)regs[i.rs1.id]); break;
        case FR::FCVT_D_L: set_d(i.rd.id, (double)(int64_t)regs[i.rs1.id]); break;
        case FR::FCVT_S_D: set_f(i.rd.id, (float)get_d(i.rs1.id)); break;
        case FR::FCVT_D_S: set_d(i.rd.id, (double)get_f(i.rs1.id)); break;

        // FMV: bitcast between freg and reg
        case FR::FMV_X_W: {
            uint32_t v;
            std::memcpy(&v, &fregs[i.rs1.id], 4);
            regs[i.rd.id] = (int64_t)(int32_t)v;
            break;
        }
        case FR::FMV_W_X: {
            uint32_t v = (uint32_t)regs[i.rs1.id];
            std::memcpy(&fregs[i.rd.id], &v, 4);
            break;
        }
    }
}

void VirtualMachine::exec_i(const InstI& i, const FlatInst& fi) {
    using I = OpI;
    switch (i.op) {
        // ---- Arithmetic immediate ----
        case I::ADDI: regs[i.rd.id] = (int64_t)regs[i.rs1.id] + (int64_t)i.imm; break;
        case I::ADDIW: regs[i.rd.id] = (int64_t)(int32_t)((int32_t)regs[i.rs1.id] + i.imm); break;
        case I::SLTI: regs[i.rd.id] = (int64_t)regs[i.rs1.id] < (int64_t)i.imm; break;
        case I::SLTIU: regs[i.rd.id] = regs[i.rs1.id] < (uint64_t)(int64_t)i.imm; break;
        case I::XORI: regs[i.rd.id] = regs[i.rs1.id] ^ (uint64_t)(int64_t)i.imm; break;
        case I::ORI: regs[i.rd.id] = regs[i.rs1.id] | (uint64_t)(int64_t)i.imm; break;
        case I::ANDI: regs[i.rd.id] = regs[i.rs1.id] & (uint64_t)(int64_t)i.imm; break;
        case I::SLLI: regs[i.rd.id] = regs[i.rs1.id] << (i.imm & 0x3F); break;
        case I::SRLI: regs[i.rd.id] = regs[i.rs1.id] >> (i.imm & 0x3F); break;
        case I::SRAI: regs[i.rd.id] = (uint64_t)((int64_t)regs[i.rs1.id] >> (i.imm & 0x3F)); break;

        // W-variant shifts (5-bit shamt)
        case I::SLLIW:
            regs[i.rd.id] = (int64_t)(int32_t)((int32_t)regs[i.rs1.id] << (i.imm & 0x1F));
            break;
        case I::SRLIW:
            regs[i.rd.id] = (int64_t)(int32_t)((uint32_t)regs[i.rs1.id] >> (i.imm & 0x1F));
            break;
        case I::SRAIW:
            regs[i.rd.id] = (int64_t)(int32_t)((int32_t)regs[i.rs1.id] >> (i.imm & 0x1F));
            break;

        // ---- Memory loads ----
        case I::LD: regs[i.rd.id] = load<int64_t>(regs[i.rs1.id] + i.imm); break;
        case I::LW: regs[i.rd.id] = (int64_t)(int32_t)load<int32_t>(regs[i.rs1.id] + i.imm); break;
        case I::LWU: regs[i.rd.id] = load<uint32_t>(regs[i.rs1.id] + i.imm); break;
        case I::LH: regs[i.rd.id] = (int64_t)(int16_t)load<int16_t>(regs[i.rs1.id] + i.imm); break;
        case I::LHU: regs[i.rd.id] = load<uint16_t>(regs[i.rs1.id] + i.imm); break;
        case I::LB: regs[i.rd.id] = (int64_t)(int8_t)load<int8_t>(regs[i.rs1.id] + i.imm); break;
        case I::LBU: regs[i.rd.id] = load<uint8_t>(regs[i.rs1.id] + i.imm); break;

        // ---- Memory stores (rd = source data, rs1 = base address) ----
        case I::SD: store<int64_t>(regs[i.rs1.id] + i.imm, regs[i.rd.id]); break;
        case I::SW: store<int32_t>(regs[i.rs1.id] + i.imm, (int32_t)regs[i.rd.id]); break;
        case I::SH: store<int16_t>(regs[i.rs1.id] + i.imm, (int16_t)regs[i.rd.id]); break;
        case I::SB: store<int8_t>(regs[i.rs1.id] + i.imm, (int8_t)regs[i.rd.id]); break;

        // ---- Jump and link register ----
        case I::JALR: {
            uint64_t tmp = pc;
            pc = (regs[i.rs1.id] + i.imm) & ~1ULL;
            if (i.rd.id != 0) regs[i.rd.id] = tmp;
            break;
        }

        // ---- W-variant R-type (imm encodes rs2 register index) ----
        case I::ADDW: {
            uint32_t rs2v = (uint32_t)regs[(uint8_t)i.imm];
            regs[i.rd.id] = (int64_t)(int32_t)((int32_t)regs[i.rs1.id] + (int32_t)rs2v);
            break;
        }
        case I::SUBW: {
            uint32_t rs2v = (uint32_t)regs[(uint8_t)i.imm];
            regs[i.rd.id] = (int64_t)(int32_t)((int32_t)regs[i.rs1.id] - (int32_t)rs2v);
            break;
        }
        case I::SLLW: {
            uint32_t rs2v = (uint32_t)regs[(uint8_t)i.imm];
            regs[i.rd.id] = (int64_t)(int32_t)((int32_t)regs[i.rs1.id] << (rs2v & 0x1F));
            break;
        }
        case I::SRLW: {
            uint32_t rs2v = (uint32_t)regs[(uint8_t)i.imm];
            regs[i.rd.id] = (int64_t)(int32_t)((uint32_t)regs[i.rs1.id] >> (rs2v & 0x1F));
            break;
        }
        case I::SRAW: {
            uint32_t rs2v = (uint32_t)regs[(uint8_t)i.imm];
            regs[i.rd.id] = (int64_t)(int32_t)((int32_t)regs[i.rs1.id] >> (rs2v & 0x1F));
            break;
        }
        case I::MULW: {
            uint32_t rs2v = (uint32_t)regs[(uint8_t)i.imm];
            regs[i.rd.id] = (int64_t)(int32_t)((int32_t)regs[i.rs1.id] * (int32_t)rs2v);
            break;
        }
        case I::DIVW: {
            uint32_t rs2v = (uint32_t)regs[(uint8_t)i.imm];
            int32_t a = (int32_t)regs[i.rs1.id];
            int32_t b = (int32_t)rs2v;
            if (b == 0) {
                regs[i.rd.id] = ~0ULL;
            } else if (a == INT32_MIN && b == -1) {
                regs[i.rd.id] = (int64_t)a;
            } else {
                regs[i.rd.id] = (int64_t)(a / b);
            }
            break;
        }
        case I::REMW: {
            uint32_t rs2v = (uint32_t)regs[(uint8_t)i.imm];
            int32_t a = (int32_t)regs[i.rs1.id];
            int32_t b = (int32_t)rs2v;
            if (b == 0) {
                regs[i.rd.id] = (int64_t)a;
            } else if (a == INT32_MIN && b == -1) {
                regs[i.rd.id] = 0;
            } else {
                regs[i.rd.id] = (int64_t)(a % b);
            }
            break;
        }

        // Branch instructions — should not appear in InstI form after isel
        case I::BEQ:
        case I::BNE:
        case I::BLT:
        case I::BGE:
        case I::BLTU:
        case I::BGEU: break;
    }
}

void VirtualMachine::exec_fi(const InstFI& i) {
    // rs1.id encodes a GPR index (the base address register)
    uint64_t addr = regs[i.rs1.id] + (int64_t)i.imm;
    switch (i.op) {
        case OpFI::FLW: set_f(i.rd.id, load<float>(addr)); break;
        case OpFI::FLD: set_d(i.rd.id, load<double>(addr)); break;
        case OpFI::FSW: store<float>(addr, get_f(i.rd.id)); break;
        case OpFI::FSD: store<double>(addr, get_d(i.rd.id)); break;
    }
}

void VirtualMachine::exec_j(const InstJ& i, const FlatInst& fi) {
    regs[i.rd.id] = pc;  // return address
    pc = fi.branch_target;
}

void VirtualMachine::exec_u(const InstU& i) {
    switch (i.op) {
        case OpU::LUI: regs[i.rd.id] = (int64_t)i.imm << 12; break;
        case OpU::AUIPC: regs[i.rd.id] = (int64_t)(pc - 1) + ((int64_t)i.imm << 12); break;
    }
}

// ---------------------------------------------------------------------------
// Pseudo-instruction implementations
// ---------------------------------------------------------------------------

void VirtualMachine::exec_pseudo_r(const PseudoR& i) {
    using P = PseudoR;
    switch (i.op) {
        case P::MV: regs[i.rd.id] = regs[i.rs1.id]; break;
        case P::NOT: regs[i.rd.id] = ~regs[i.rs1.id]; break;
        case P::NEG: regs[i.rd.id] = (uint64_t)(-(int64_t)regs[i.rs1.id]); break;
        case P::NEGW: regs[i.rd.id] = (int64_t)(int32_t)(-(int32_t)regs[i.rs1.id]); break;
        case P::SEQZ: regs[i.rd.id] = regs[i.rs1.id] == 0; break;
        case P::SNEZ: regs[i.rd.id] = regs[i.rs1.id] != 0; break;
    }
}

void VirtualMachine::exec_pseudo_li(const PseudoLI& i) {
    regs[i.rd.id] = (uint64_t)i.imm;
}

void VirtualMachine::exec_pseudo_b(const PseudoB& i, const FlatInst& fi) {
    bool taken = (i.op == PseudoB::BEQZ) ? (regs[i.rs1.id] == 0) : (regs[i.rs1.id] != 0);
    if (taken) pc = fi.branch_target;
    // else fall through to next instruction (the `j false_target`)
}

void VirtualMachine::exec_pseudo_j(const PseudoJ& i, const FlatInst& fi) {
    switch (i.op) {
        case PseudoJ::J: pc = fi.branch_target; break;
        case PseudoJ::CALL:
            if (fi.is_call_builtin) {
                builtins[i.target](*this);
            } else {
                call_stack.push_back(pc);
                pc = fi.branch_target;
            }
            break;
    }
}

void VirtualMachine::exec_pseudo_l(const PseudoL& i) {
    using P = PseudoL;
    auto addr = symbol_map[i.symbol];
    switch (i.op) {
        case P::LA: regs[i.rd.id] = addr; break;
        case P::LGD: regs[i.rd.id] = load<int64_t>(addr); break;
        case P::LGW: regs[i.rd.id] = (int64_t)(int32_t)load<int32_t>(addr); break;
        case P::SGD: store<int64_t>(addr, regs[i.rd.id]); break;
        case P::SGW: store<int32_t>(addr, (int32_t)regs[i.rd.id]); break;
    }
}

void VirtualMachine::exec_pseudo_ret() {
    if (call_stack.empty()) {
        pc = flat_insts.size();  // exit main loop
    } else {
        pc = call_stack.back();
        call_stack.pop_back();
    }
}

}  // namespace rv64::vm
