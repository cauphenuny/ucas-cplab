#pragma once
#include "backend/ir/analysis/cfg.hpp"
#include "backend/ir/analysis/dataflow/liveness.hpp"
#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/lowering/regalloc/precolorize.hpp"

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace ir::lowering {

struct InterfereNode {
    LeftValue value;
    std::unordered_set<LeftValue> neighbors;
    std::optional<size_t> color;
    TO_STRING(InterfereNode, value, neighbors, color);
};

struct InterfereGraph {
    explicit InterfereGraph(TargetABI abi) : abi(std::move(abi)) {}
    void ensure(const LeftValue& value) {
        if (nodes_.count(value) == 0) {
            nodes_[value] = InterfereNode{.value = value};
        }
    }
    void connect(const LeftValue& u, const LeftValue& v) {
        if (u == v) return;
        ensure(u), ensure(v);
        nodes_[u].neighbors.insert(v);
        nodes_[v].neighbors.insert(u);
    }
    [[nodiscard]] bool interferes(const LeftValue& u, const LeftValue& v) {
        ensure(u), ensure(v);
        return nodes_[u].neighbors.count(v) > 0;
    }
    void pin(const LeftValue& value, size_t color) {
        ensure(value);
        nodes_[value].color = color;
    }
    const InterfereNode& operator[](const LeftValue& value) {
        ensure(value);
        return nodes_.at(value);
    }

    [[nodiscard]] size_t max_color(const Type& type) const {
        return abi.reg_of(type).size;
    }

    static InterfereGraph from_precolored(const ProxyMap& precolor, const TargetABI& abi) {
        InterfereGraph graph(abi);
        for (const auto& [key, alloc] : precolor) {
            auto& [type, id] = key;
            graph.pin(alloc->value(), id);
        }
        for (auto& node : graph.nodes_) {
            for (auto& other : graph.nodes_) {
                graph.connect(node.first, other.first);
            }
        }
        return graph;
    }

    static InterfereGraph build(Program& program, const ProxyMap& proxies,
                                const TargetABI& abi) {
        using namespace analysis;
        auto graph = from_precolored(proxies, abi);

        for (auto& func : program.funcs()) {
            auto cfg = ControlFlowGraph(*func);
            auto liveness = DataFlow<flow::Liveness>(cfg, program);
            for (auto& block : func->blocks()) {
                auto live = liveness.out.at(block.get());
                if (auto exit_use = utils::used_var(block->exit())) {
                    live.insert(*exit_use);
                }
                for (auto inst_it = block->insts().rbegin(); inst_it != block->insts().rend();
                     ++inst_it) {
                    auto& inst = *inst_it;

                    auto def = *utils::defined_var(inst);
                    live.erase(def);

                    for (auto& l : live) {
                        graph.connect(l, def);
                    }
                    // forbid live variables cross call-inst to be colored as caller-saved registers
                    if (std::holds_alternative<CallInst>(inst)) {
                        for (auto& l : live) {
                            auto& regs = abi.reg_of(type_of(l));
                            for (auto reg : regs.caller_saved) {
                                graph.connect(l, proxies.at({type_of(l), reg})->value());
                            }
                        }
                    }

                    // phi-inst's uses is added by block-wise liveness analysis
                    if (std::holds_alternative<PhiInst>(inst)) continue;

                    for (const auto& use : utils::used_vars(inst)) {
                        live.insert(*use);
                    }
                }
            }
        }

        for (auto& node : graph.nodes()) {
            auto type = type_of(node.first);
            for (auto reserved : abi.reg_of(type).reserved) {
                graph.connect(node.first, proxies.at({type, reserved})->value());
            }
        }

        return graph;
    }
    [[nodiscard]] const std::unordered_map<LeftValue, InterfereNode>& nodes() const {
        return nodes_;
    }

    TO_STRING(InterfereGraph, nodes_);

private:
    std::unordered_map<LeftValue, InterfereNode> nodes_;
    TargetABI abi;
};

}  // namespace ir::lowering
