#pragma once

#include "backend/ir/ir.h"
#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/ir/type.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <set>
#include <vector>

namespace ir::lowering {

using ColorMap = std::unordered_map<LeftValue, ssize_t>;
Type register_type(const Type& ir_type) {
    return is_fp(ir_type) ? type::floating() : type::integer();
}

struct PrecolorVars {
    struct ProxyKeyHasher {
        size_t operator()(const std::pair<Type, size_t>& key) const {
            auto& [type, id] = key;
            size_t h2 = std::hash<size_t>{}(id);
            size_t h3 = std::hash<bool>{}(is_fp(type));
            return (h2 << 1) ^ (h3 << 2);
        }
    };

    std::unordered_map<std::pair<Type, size_t>, Alloc*, ProxyKeyHasher> proxies;
    Alloc*& operator[](const std::pair<Type, size_t>& key) {
        return proxies[{register_type(key.first), key.second}];
    }
    auto begin() {
        return proxies.begin();
    }
    auto begin() const {
        return proxies.begin();
    }
    auto end() {
        return proxies.end();
    }
    auto end() const {
        return proxies.end();
    }

    [[nodiscard]] Alloc* const& at(const std::pair<Type, size_t>& key) const {
        return proxies.at({register_type(key.first), key.second});
    }
};

/// @note: colorized: func call, func param, return value, callee-saved registers

struct Precolorize : ir::transform::NonSSAPass {
    Precolorize(TargetABI abi, bool verbose = false) : abi(std::move(abi)), verbose(verbose) {}
    bool apply(Program& prog, ir::transform::NonSSAPassContext& ctx) override {
        map_registers(prog);
        for (auto& func : prog.funcs()) {
            colorize_param(*func, prog);
            for (auto& block : func->blocks()) {
                colorize_call(*block, *func, prog);
            }
            auto exits = colorize_return(*func, prog);
            colorize_callee_saved(*func, prog, exits);
        }
        if (verbose) {
            fmt::println(stderr, "After precoloring:\n{}", prog);
        }
        return true;
    }
    PrecolorVars precolored;

private:
    TargetABI abi;
    bool verbose;

    void map_registers(Program& prog) {
        auto map = [&](const Type& type) {
            auto& regs = abi.reg_of(type);
            for (size_t idx = 0; idx < regs.size; idx++) {
                auto name = fmt::format("__reg_{}", regs.name(idx));
                auto proxy = Alloc::variable(name, type, std::nullopt, true);
                precolored[{type, idx}] = proxy.get();
                prog.addGlobal(std::move(proxy));
            }
        };
        map(type::integer());
        map(type::floating());
    }

    void colorize_param(Func& func, Program& prog) {
        auto regs = assign_param_regs(func.params, abi);
        for (size_t i = 0; i < func.params.size(); i++) {
            if (!regs[i]) continue;
            auto proxy = precolored[{func.params[i]->type, regs[i].value()}];
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
                LeftValue proxy = precolored[{type, abi.reg_of(type).return_value}]->value();
                block->append(
                    UnaryInst{.op = UnaryInstOp::MOV, .result = proxy, .operand = ret->exp});
                block->setExit(ReturnExit{proxy});

                ret_blocks.push_back(block.get());
            }
        }
        return ret_blocks;
    }

    void colorize_callee_saved(Func& func, Program& prog, const std::vector<Block*>& ret_blocks) {
        auto save = [&](const std::set<size_t>& regs, const char* prefix, const Type& type) {
            for (auto idx : regs) {
                auto backup = func.newTemp(type, func.entrance());
                func.entrance()->prepend(
                    UnaryInst{.op = UnaryInstOp::MOV,
                              .result = LeftValue{backup},
                              .operand = LeftValue{precolored[{type, idx}]->value()}});
                for (auto ret : ret_blocks) {
                    ret->append(UnaryInst{.op = UnaryInstOp::MOV,
                                          .result = LeftValue{precolored[{type, idx}]->value()},
                                          .operand = LeftValue{backup}});
                }
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
                std::vector<Value> args = call->args;
                auto next_it = block.erase(it);

                auto regs = assign_arg_regs(args, abi);
                for (size_t i = 0; i < args.size(); i++) {
                    if (!regs[i]) continue;
                    auto reg = regs[i].value();
                    LeftValue proxy = precolored[{type_of(args[i]), reg}]->value();
                    block.insert(
                        next_it,
                        UnaryInst{.op = UnaryInstOp::MOV, .result = proxy, .operand = args[i]});
                    args[i] = proxy;
                }
                if (result) {
                    auto reg = abi.reg_of(type_of(*result)).return_value;
                    LeftValue retval = precolored[{type_of(*result), reg}]->value();
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
