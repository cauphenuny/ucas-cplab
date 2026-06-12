/// @note Array Initialization Lowering
/// Must run after AddStandardLib
#pragma once

#include "backend/ir/ir.h"
#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/ir/type.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace ir::lowering {

template <typename T> struct ArrayInitLowering : transform::Pass<T> {
    bool apply(Program& prog, T& ctx) override {
        bool changed = false;
        for (auto& func : prog.funcs()) changed |= apply_func(prog, *func, ctx);
        return changed;
    }

    const TargetABI abi;

    ArrayInitLowering(TargetABI abi) : abi(std::move(abi)) {}

private:
    bool apply_func(Program& prog, Func& func, T& ctx) {
        bool changed = false;
        for (auto& block : func.blocks()) changed |= apply_block(prog, func, *block, ctx);
        return changed;
    }

    bool apply_block(Program& prog, Func& func, Block& block, T& ctx) {
        bool changed = false;
        size_t cnt = 0;
        for (auto it = block.insts().begin(); it != block.insts().end(); ++it) {
            auto& inst = *it;
            if (auto binary = std::get_if<BinaryInst>(&inst)) {
                if (binary->op != InstOp::STORE) continue;
                if (!type_of(binary->rhs).is<type::Array>()) continue;
                auto type = type_of(binary->rhs);
                auto size = (int64_t)abi.mem.size(type);
                changed = true;
                auto init = std::get<ConstexprValue>(binary->rhs);
                CallInst memset = {.result = std::nullopt,
                                   .func = prog.findBuiltin("memset").value(),
                                   .args =
                                       std::vector<Value>{binary->lhs, ConstexprValue((int64_t)0),
                                                          ConstexprValue(size)}};
                if (init == ConstexprValue::zeros_like(init.type)) {
                    block.replace(&inst, memset);
                    continue;
                } else {
                    auto pruned = init.prune();
                    auto prune_size = (int64_t)abi.mem.size(pruned.type);
                    if (prune_size != size) {
                        block.insert(it, memset);
                    }
                    auto proxy = Alloc::constant(fmt::format("__init_{}_{}", func.name, cnt++),
                                                 pruned.type, pruned, true);
                    CallInst memcpy = {.result = std::nullopt,
                                       .func = prog.findBuiltin("memcpy").value(),
                                       .args = std::vector<Value>{
                                           binary->lhs, LeftValue{proxy->value()}, prune_size}};
                    block.replace(&inst, memcpy);
                    prog.addGlobal(std::move(proxy));
                }
            }
        }
        return changed;
    }
};

}  // namespace ir::lowering