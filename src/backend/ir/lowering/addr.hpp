/// @note Lower BORROW_ELEM / BORROW_ELEM_MUT into integer address arithmetic.
/// Must run before DestructSSA.
#pragma once

#include "backend/ir/ir.h"
#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/ir/type.hpp"

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
                LeftValue offset_bytes = func.newTemp(type::int32(), &block);
                auto elem_type = match(
                    type_of(base).var(), [](const type::Reference& ref) { return ref.elem; },
                    [](const type::Array& arr) { return arr.elem; },
                    [&](auto&) -> Type {
                        throw COMPILER_ERROR(fmt::format(
                            "Expected array or reference type, but got {}", type_of(base)));
                    });
                LeftValue base_int = func.newTemp(type::integer(), &block);
                LeftValue result_int = func.newTemp(type::integer(), &block);
                block.insert(it, BinaryInst{.op = InstOp::MUL,
                                            .result = offset_bytes,
                                            .lhs = offset,
                                            .rhs = ConstexprValue((int)abi.mem.size(elem_type))});
                block.insert(
                    it, UnaryInst{.op = UnaryInstOp::CONVERT, .result = base_int, .operand = base});
                block.insert(it, BinaryInst{.op = InstOp::ADD,
                                            .result = result_int,
                                            .lhs = base_int,
                                            .rhs = offset_bytes});
                block.replace(
                    &inst,
                    UnaryInst{.op = UnaryInstOp::CONVERT, .result = result, .operand = result_int});
            }
        }
        return changed;
    }
};

}  // namespace ir::lowering
