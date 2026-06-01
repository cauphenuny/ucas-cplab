#pragma once

#include "backend/ir/ir.h"
#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/ir/type.hpp"

#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ir::lowering {

using ColorMap = std::unordered_map<LeftValue, ssize_t>;
struct ProxyKeyHasher {
    size_t operator()(const std::pair<Type, size_t>& key) const {
        auto& [type, id] = key;
        size_t h2 = std::hash<size_t>{}(id);
        size_t h3 = std::hash<bool>{}(is_fp(type));
        return (h2 << 1) ^ (h3 << 2);
    }
};
using ProxyMap = std::unordered_map<std::pair<Type, size_t>, Alloc*, ProxyKeyHasher>;

/// @note: colorized: func call, func param, return value

struct PreColorize : ir::transform::NonSSAPass {
    PreColorize(TargetABI abi) : abi(std::move(abi)) {}
    bool apply(Program& prog, ir::transform::NonSSAPassContext& ctx) override {
        map_registers(prog);
        for (auto& func : prog.funcs()) {
            colorize_param(*func, prog);
            for (auto& block : func->blocks()) {
                colorize_call(*block, *func, prog);
            }
            colorize_return(*func, prog);
        }
        return true;
    }
    ProxyMap proxies;

private:
    TargetABI abi;

    auto proxy(Program& program, const Type& type, size_t id) {
        auto& regs = abi.reg_of(type);
        auto index = std::make_tuple(type, id);
        if (proxies.count(index) == 0) {
            auto name = fmt::format("__reg_{}_{}", type, regs.name(id));
            auto proxy = Alloc::variable(name, type, std::nullopt, true);
            proxies[index] = proxy.get();
            program.addGlobal(std::move(proxy));
        }
        return proxies[index];
    }

    void map_registers(Program& prog) {
        auto map = [&](const Type& type) {
            for (size_t idx = 0; idx < abi.reg_of(type).size; idx++) {
                proxy(prog, type, idx);
            }
        };
        map(type::construct<bool>());
        map(type::construct<int>());
        map(type::construct<float>());
        map(type::construct<double>());
    }

    void colorize_param(Func& func, Program& prog) {
        auto regs = assign_param_regs(func.params, abi);
        for (size_t i = 0; i < func.params.size(); i++) {
            if (!regs[i]) continue;
            auto proxy = proxies[{func.params[i]->type, regs[i].value()}];
            func.entrance()->prepend(UnaryInst{.op = UnaryInstOp::MOV,
                                               .result = LeftValue{func.params[i]->value()},
                                               .operand = LeftValue{proxy->value()}});
        }
    }

    std::vector<Block*> colorize_return(Func& func, Program& prog) {
        std::vector<Block*> ret_blocks;
        for (auto& block : func.blocks()) {
            if (auto ret = std::get_if<ReturnExit>(&block->exit())) {
                auto type = type_of(ret->exp);
                LeftValue proxy = proxies[{type, abi.reg_of(type).return_value}]->value();
                block->append(
                    UnaryInst{.op = UnaryInstOp::MOV, .result = proxy, .operand = ret->exp});
                block->setExit(ReturnExit{proxy});

                ret_blocks.push_back(block.get());
            }
        }
        return ret_blocks;
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
                    LeftValue proxy = proxies[{type_of(args[i]), reg}]->value();
                    block.insert(
                        next_it,
                        UnaryInst{.op = UnaryInstOp::MOV, .result = proxy, .operand = args[i]});
                    args[i] = proxy;
                }
                if (result) {
                    auto reg = abi.reg_of(type_of(*result)).return_value;
                    LeftValue retval = proxies[{type_of(*result), reg}]->value();
                    block.insert(next_it, CallInst{retval, callee, std::move(args)});
                    block.insert(
                        next_it,
                        UnaryInst{.op = UnaryInstOp::MOV, .result = result, .operand = retval});
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
