#pragma once

#include "backend/ir/ir.h"
#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/ir/type.hpp"

#include <optional>
#include <set>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ir::lowering {

using ColorMap = std::unordered_map<LeftValue, ssize_t>;

/// @note: colorized: func call, func param, return value, callee-saved registers

struct PreColorize : ir::transform::NonSSAPass {
    PreColorize(TargetABI abi) : abi(std::move(abi)) {}
    bool apply(Program& prog, ir::transform::NonSSAPassContext& ctx) override {
        for (auto& func : prog.funcs()) {
            colorize_param(*func, prog);
            for (auto& block : func->blocks()) {
                colorize_call(*block, *func, prog);
            }
            auto exits = colorize_return(*func, prog);
            colorize_callee_saved(*func, prog, exits);
        }
        return true;
    }
    ColorMap precolored;

private:
    TargetABI abi;

    void colorize_param(Func& func, Program& prog) {
        auto regs = assign_param_regs(func.params, abi);
        for (size_t i = 0; i < func.params.size(); i++) {
            if (!regs[i]) continue;
            auto reg = regs[i].value();
            auto name = fmt::format("__arg_{}{}", is_fp(func.params[i]->type) ? "f" : "x", reg);
            auto proxy = Alloc::variable(name, func.params[i]->type, std::nullopt, true);
            precolored[proxy->value()] = reg;
            func.entrance()->prepend(UnaryInst{.op = UnaryInstOp::MOV,
                                               .result = LeftValue{func.params[i]->value()},
                                               .operand = LeftValue{proxy->value()}});
            func.addLocal(std::move(proxy));
        }
    }

    std::vector<Block*> colorize_return(Func& func, Program& prog) {
        std::vector<Block*> ret_blocks;
        for (auto& block : func.blocks()) {
            if (auto ret = std::get_if<ReturnExit>(&block->exit())) {
                auto type = type_of(ret->exp);
                auto temp = LeftValue{func.newTemp(type, block.get())};
                block->append(
                    UnaryInst{.op = UnaryInstOp::MOV, .result = temp, .operand = ret->exp});
                block->setExit(ReturnExit{temp});
                precolored[temp] = abi.reg_of(type).return_value;

                ret_blocks.push_back(block.get());
            }
        }
        return ret_blocks;
    }

    void colorize_callee_saved(Func& func, Program& prog, const std::vector<Block*>& ret_blocks) {
        auto save = [&](const std::set<size_t>& regs, const char* prefix, const Type& type) {
            for (auto idx : regs) {
                auto name = fmt::format("__save_{}{}", prefix, idx);
                auto proxy = Alloc::variable(name, type);
                precolored[proxy->value()] = idx;
                auto backup = func.newTemp(proxy->type, func.entrance());
                func.entrance()->prepend(UnaryInst{.op = UnaryInstOp::MOV,
                                                   .result = LeftValue{backup},
                                                   .operand = LeftValue{proxy->value()}});
                for (auto ret : ret_blocks) {
                    ret->append(UnaryInst{.op = UnaryInstOp::MOV,
                                          .result = LeftValue{proxy->value()},
                                          .operand = LeftValue{backup}});
                }
                func.addLocal(std::move(proxy));
            }
        };
        save(abi.reg.generals.callee_saved, "x", type::construct<int>());
        save(abi.reg.floats.callee_saved, "f", type::construct<double>());
    }

    void colorize_call(Block& block, Func& func, Program& prog) {
        for (auto it = block.insts().begin(); it != block.insts().end();) {
            auto& inst = *it;
            if (auto call = std::get_if<CallInst>(&inst)) {
                // Extract data before erasing (erase destroys the instruction)
                auto result = call->result;
                auto callee = call->func;
                std::vector<Value> args = std::move(call->args);
                auto next_it = block.erase(it);

                auto regs = assign_arg_regs(args, abi);
                for (size_t i = 0; i < args.size(); i++) {
                    if (!regs[i]) continue;
                    auto reg = regs[i].value();
                    auto temp = LeftValue{func.newTemp(type_of(args[i]), &block)};
                    block.insert(
                        next_it,
                        UnaryInst{.op = UnaryInstOp::MOV, .result = temp, .operand = args[i]});
                    precolored[temp] = reg;
                    args[i] = temp;
                }
                if (type_of(result).is<type::Primitive>()) {
                    auto reg = abi.reg_of(type_of(result)).return_value;
                    auto retval = LeftValue{func.newTemp(type_of(result), &block)};
                    block.insert(next_it, CallInst{retval, callee, std::move(args)});
                    block.insert(
                        next_it,
                        UnaryInst{.op = UnaryInstOp::MOV, .result = result, .operand = retval});
                    precolored[retval] = reg;
                } else {
                    block.insert(next_it, CallInst{result, callee, std::move(args)});
                }
                it = next_it;
            } else {
                it = std::next(it);
            }
        }
    }
};

}  // namespace ir::lowering
