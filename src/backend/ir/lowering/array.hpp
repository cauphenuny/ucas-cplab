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
        for (auto& inst : block.insts()) {
            if (auto binary = std::get_if<BinaryInst>(&inst)) {
                if (binary->op != InstOp::STORE) continue;
                if (!type_of(binary->rhs).is<type::Array>()) continue;
                auto type = type_of(binary->rhs);
                auto size = (int64_t)abi.mem.size(type);
                changed = true;
                auto init = std::get<ConstexprValue>(binary->rhs);
                if (init == ConstexprValue::zeros_like(init.type)) {
                    auto memset = prog.findBuiltin("memset").value();
                    std::vector<Value> args = {binary->lhs, ConstexprValue((int64_t)0),
                                               ConstexprValue(size)};
                    block.replace(
                        &inst,
                        CallInst{.result = std::nullopt, .func = memset, .args = std::move(args)});
                    continue;
                } else {
                    auto proxy = Alloc::constant(fmt::format("__init_{}_{}", func.name, cnt++),
                                                 type, std::get<ConstexprValue>(binary->rhs), true);
                    auto memcpy = prog.findBuiltin("memcpy").value();
                    std::vector<Value> args = {binary->lhs, LeftValue{proxy->value()}, size};
                    block.replace(
                        &inst,
                        CallInst{.result = std::nullopt, .func = memcpy, .args = std::move(args)});
                    prog.addGlobal(std::move(proxy));
                }
            }
        }
        return changed;
    }
};

}  // namespace ir::lowering