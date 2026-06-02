/// @note Lower BORROW_ELEM / BORROW_ELEM_MUT into integer address arithmetic.
/// Must run before DestructSSA.
#pragma once

#include "backend/ir/ir.h"
#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/transform/framework.hpp"

namespace ir::lowering {

struct AddressLowering : transform::SSAPass {
    AddressLowering(const TargetABI& abi) : abi(abi) {}

    bool apply(Program& prog, transform::SSAPassContext& ctx) override {
        bool changed = false;
        for (auto& func : prog.funcs()) changed |= apply_func(*func, ctx);
        return changed;
    }

private:
    const TargetABI& abi;

    bool apply_func(Func& func, transform::SSAPassContext& ctx) {
        bool changed = false;
        for (auto& block : func.blocks()) changed |= apply_block(func, *block, ctx);
        return changed;
    }

    bool apply_block(Func& func, Block& block, transform::SSAPassContext& ctx) {
        bool changed = false;
        for (auto it = block.insts().begin(); it != block.insts().end(); ++it) {
            auto& inst = *it;
            if (auto* binary = std::get_if<BinaryInst>(&inst)) {
                if (binary->op != InstOp::BORROW_ELEM && binary->op != InstOp::BORROW_ELEM_MUT)
                    continue;
                changed = true;
                auto base = binary->lhs;
                auto offset = binary->rhs;
                auto result = *binary->result;
                LeftValue offset_bytes = func.newTemp(type::construct<int>(), &block);
                block.insert(
                    it, BinaryInst{.op = InstOp::MUL,
                                   .result = offset_bytes,
                                   .lhs = offset,
                                   .rhs = ConstexprValue((int)abi.mem.size(type_of(base)))});
                block.replace(&inst, BinaryInst{.op = InstOp::ADD,
                                                .result = result,
                                                .lhs = base,
                                                .rhs = offset_bytes});
                ctx.ud.replace_all_uses_with(result, LeftValue{result});
            }
        }
        return changed;
    }
};

}  // namespace ir::lowering
