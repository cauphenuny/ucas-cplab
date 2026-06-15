#pragma once

#include "backend/rv64/inst.hpp"
#include "framework.hpp"
#include "utils/match.hpp"

#include <optional>
#include <vector>

namespace rv64::optim {

// ---------------------------------------------------------------------------
// peephole framework
// ---------------------------------------------------------------------------

// Apply a match_fn over every window of `window` instructions in `ii`.
// match_fn returns std::nullopt on no match, or a replacement vector.
// Rules run in the order declared, each until saturation (single pass back-to-front).
template <typename F> bool peephole_apply(std::vector<Inst>& insts, int window, F&& match_fn) {
    bool changed = false;
    for (int i = (int)insts.size() - window; i >= 0; i--) {
        if (i + window > (int)insts.size()) continue;
        auto repl = match_fn(insts, i);
        if (!repl) continue;
        insts.erase(insts.begin() + i, insts.begin() + i + window);
        insts.insert(insts.begin() + i, repl->begin(), repl->end());
        changed = true;
    }
    return changed;
}

// ---------------------------------------------------------------------------
// RedundantJumpElimination
// ---------------------------------------------------------------------------

/*
from:
    j label
    label:
to:
    label:
*/
struct RedundantJumpElimination : Pass {
    bool apply(Module& mod) override {
        bool changed = false;
        for (auto& func : mod.funcs) {
            for (size_t i = 0; i + 1 < func.blocks.size(); i++) {
                auto last = func.blocks[i].insts.back();
                if (auto j = std::get_if<PseudoJ>(&last)) {
                    if (j->op == PseudoJ::J && j->target == func.blocks[i + 1].label) {
                        func.blocks[i].insts.pop_back();
                        changed = true;
                    }
                }
            }
        }
        return changed;
    }
};

// ---------------------------------------------------------------------------
// BranchCondSimplification — three sub-rules, applied in order
// ---------------------------------------------------------------------------

/*
#1
from:
    cmp x, ...        (slt/sltu/slti/sltiu/seqz/snez)
    xori x, x, 1
    bnez/beqz x, label
to:
    cmp x, ...
    beqz/bnez x, label

#2
from:
    slt/sltu x, y, z
    bnez/beqz x, label
to:
    blt/bge/bltu/bgeu y, z, label

#3
from:
    seqz/snez x, y
    bnez/beqz x, label
to:
    beqz/bnez y, label
*/
struct BranchCondSimplification : Pass {
    bool apply(Module& mod) override {
        bool changed = false;
        for (auto& func : mod.funcs) {
            for (auto& block : func.blocks) {
                auto& ii = block.insts;
                changed |= peephole_apply(ii, 3, rule1);
                changed |= peephole_apply(ii, 2, rule2);
                changed |= peephole_apply(ii, 2, rule3);
            }
        }
        return changed;
    }

private:
    using Opt = std::optional<std::vector<Inst>>;

    // #1: cmp + xori x, x, 1 + bnez/beqz x, L  →  cmp + beqz/bnez x, L
    static Opt rule1(const std::vector<Inst>& ii, int i) {
        return Match{ii[i], ii[i + 1], ii[i + 2]}(
            [](const auto& cmp, const InstI& xori, const PseudoB& br) -> Opt {
                if (xori.op != OpI::XORI || xori.imm != 1 || xori.rd.id != xori.rs1.id) return {};
                auto r = cmp_rd(cmp);
                if (!r || *r != xori.rd.id || *r != br.rs1.id) return {};
                if (br.op != PseudoB::BNEZ && br.op != PseudoB::BEQZ) return {};
                auto new_op = (br.op == PseudoB::BNEZ) ? PseudoB::BEQZ : PseudoB::BNEZ;
                return {{cmp, PseudoB{new_op, br.rs1, br.target}}};
            },
            [](const auto&, const auto&, const auto&) -> Opt { return {}; });
    }

    // #2: slt/sltu x, y, z + bnez/beqz x, L  →  blt/bge/bltu/bgeu y, z, L
    static Opt rule2(const std::vector<Inst>& ii, int i) {
        return Match{ii[i], ii[i + 1]}(
            [](const InstR& slt, const PseudoB& br) -> Opt {
                if (slt.op != OpR::SLT && slt.op != OpR::SLTU) return {};
                if (slt.rd.id != br.rs1.id) return {};
                OpB bop;
                if (slt.op == OpR::SLT && br.op == PseudoB::BNEZ)
                    bop = OpB::BLT;
                else if (slt.op == OpR::SLT && br.op == PseudoB::BEQZ)
                    bop = OpB::BGE;
                else if (slt.op == OpR::SLTU && br.op == PseudoB::BNEZ)
                    bop = OpB::BLTU;
                else if (slt.op == OpR::SLTU && br.op == PseudoB::BEQZ)
                    bop = OpB::BGEU;
                else
                    return {};
                return {{InstB{bop, slt.rs1, slt.rs2, br.target}}};
            },
            [](const auto&, const auto&) -> Opt { return {}; });
    }

    // #3: seqz/snez x, y + bnez/beqz x, L  →  beqz/bnez y, L
    static Opt rule3(const std::vector<Inst>& ii, int i) {
        return Match{ii[i], ii[i + 1]}(
            [](const PseudoR& pr, const PseudoB& br) -> Opt {
                if (pr.op != PseudoR::SEQZ && pr.op != PseudoR::SNEZ) return {};
                if (pr.rd.id != br.rs1.id) return {};
                if (br.op != PseudoB::BNEZ && br.op != PseudoB::BEQZ) return {};
                bool is_seqz = pr.op == PseudoR::SEQZ;
                bool is_bnez = br.op == PseudoB::BNEZ;
                auto new_op = (is_seqz == is_bnez) ? PseudoB::BEQZ : PseudoB::BNEZ;
                return {{PseudoB{new_op, pr.rs1, br.target}}};
            },
            [](const auto&, const auto&) -> Opt { return {}; });
    }

    static std::optional<uint8_t> cmp_rd(const Inst& inst) {
        using T = std::optional<uint8_t>;
        return Match{inst}(
            [](const InstR& r) -> T {
                if (r.op == OpR::SLT || r.op == OpR::SLTU) return r.rd.id;
                return {};
            },
            [](const InstI& i) -> T {
                if (i.op == OpI::SLTI || i.op == OpI::SLTIU) return i.rd.id;
                return {};
            },
            [](const PseudoR& p) -> T {
                if (p.op == PseudoR::SEQZ || p.op == PseudoR::SNEZ) return p.rd.id;
                return {};
            },
            [](const auto&) -> T { return {}; });
    }
};

// ---------------------------------------------------------------------------
// RedundantLoadElimination
// ---------------------------------------------------------------------------

/*
sd/sw/sh/sb x, offset(y)
ld/lw/lwu/lh/lhu/lb/lbu z, offset(y)
    =>
sd/sw/sh/sb x, offset(y)
<extract z, x>          ; when load width <= store width

Extraction uses:
  ld  (8B)            → addi z, x, 0
  lw  (signed 4B)     → addiw z, x, 0
  lwu (unsigned 4B)   → slli z, x, 32; srli z, z, 32
  lh  (signed 2B)     → slli z, x, 48; srai z, z, 48
  lhu (unsigned 2B)   → slli z, x, 48; srli z, z, 48
  lb  (signed 1B)     → slli z, x, 56; srai z, z, 56
  lbu (unsigned 1B)   → andi z, x, 0xff
*/
struct RedundantLoadElimination : Pass {
    bool apply(Module& mod) override {
        bool changed = false;
        for (auto& func : mod.funcs) {
            for (auto& block : func.blocks) {
                changed |= peephole_apply(block.insts, 2, rule);
            }
        }
        return changed;
    }

private:
    using Opt = std::optional<std::vector<Inst>>;

    static auto store_width(OpI op) -> int {
        switch (op) {
            case OpI::SD: return 8;
            case OpI::SW: return 4;
            case OpI::SH: return 2;
            case OpI::SB: return 1;
            default: return 0;
        }
    }

    static auto load_width(OpI op) -> int {
        switch (op) {
            case OpI::LD: return 8;
            case OpI::LW:
            case OpI::LWU: return 4;
            case OpI::LH:
            case OpI::LHU: return 2;
            case OpI::LB:
            case OpI::LBU: return 1;
            default: return 0;
        }
    }

    static auto gen_extract(OpI load_op, GeneralReg dst, GeneralReg src) -> std::vector<Inst> {
        switch (load_op) {
            case OpI::LD: return {InstI{OpI::ADDI, dst, src, 0}};
            case OpI::LW: return {InstI{OpI::ADDIW, dst, src, 0}};
            case OpI::LWU: return {InstI{OpI::SLLI, dst, src, 32}, InstI{OpI::SRLI, dst, dst, 32}};
            case OpI::LH: return {InstI{OpI::SLLI, dst, src, 48}, InstI{OpI::SRAI, dst, dst, 48}};
            case OpI::LHU: return {InstI{OpI::SLLI, dst, src, 48}, InstI{OpI::SRLI, dst, dst, 48}};
            case OpI::LB: return {InstI{OpI::SLLI, dst, src, 56}, InstI{OpI::SRAI, dst, dst, 56}};
            case OpI::LBU: return {InstI{OpI::ANDI, dst, src, 0xff}};
            default: return {};
        }
    }

    static Opt rule(const std::vector<Inst>& ii, int i) {
        auto* st = std::get_if<InstI>(&ii[i]);
        auto* ld = std::get_if<InstI>(&ii[i + 1]);
        if (!st || !ld) return {};

        int sw = store_width(st->op);
        int lw = load_width(ld->op);
        if (sw == 0 || lw == 0) return {};
        if (lw > sw) return {};
        if (st->rs1.id != ld->rs1.id || st->imm != ld->imm) return {};

        std::vector<Inst> repl = {*st};
        auto extr = gen_extract(ld->op, ld->rd, st->rd);
        repl.insert(repl.end(), extr.begin(), extr.end());
        return repl;
    }
};

}  // namespace rv64::optim
