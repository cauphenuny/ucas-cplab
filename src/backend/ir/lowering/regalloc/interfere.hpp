#pragma once
#include "backend/ir/analysis/cfg.hpp"
#include "backend/ir/analysis/dataflow/liveness.hpp"
#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/lowering/regalloc/precolorize.hpp"

#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace ir::lowering {

struct InterfereNode {
    LeftValue value;
    std::unordered_set<LeftValue> neighbors;
    std::set<size_t> available_colors;
    bool pinned{false};
    std::string toString() const {
        return fmt::format("value = {}, neighbors = {}, available_colors = {}, pinned = {}", value,
                           neighbors, available_colors, pinned);
    }
};

struct InterfereGraph {
    explicit InterfereGraph(TargetABI abi) : abi(std::move(abi)) {}
    void ensure(const LeftValue& value) {
        if (nodes_.count(value) == 0) {
            nodes_[value] = InterfereNode{.value = value};
            auto& regs = abi.reg_of(type_of(value));
            for (size_t i = 0; i < regs.size; i++) {
                if (regs.reserved.count(i)) continue;
                nodes_[value].available_colors.insert(i);
            }
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
    void connect(const LeftValue& value, size_t color) {
        ensure(value);
        nodes_[value].available_colors.erase(color);
    }
    void pin(const LeftValue& value) {
        ensure(value);
        nodes_[value].pinned = true;
    }
    const InterfereNode& operator[](const LeftValue& value) {
        ensure(value);
        return nodes_.at(value);
    }

    static InterfereGraph from_precolored(const ColorMap& precolor, const TargetABI& abi) {
        InterfereGraph graph(abi);
        for (const auto& [value, color] : precolor) {
            size_t size = abi.reg_of(type_of(value)).size;
            for (size_t i = 0; i < size; i++) {
                if (i == color) continue;
                graph.connect(value, i);
            }
            graph.pin(value);
        }
        return graph;
    }

    static InterfereGraph build(Program& program, const ColorMap& precolor, const TargetABI& abi) {
        using namespace analysis;
        auto graph = from_precolored(precolor, abi);

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
                                graph.connect(l, reg);
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

        return graph;
    }
    [[nodiscard]] const std::unordered_map<LeftValue, InterfereNode>& nodes() const {
        return nodes_;
    }

    [[nodiscard]] std::string toString() const {
        std::string res;
        for (const auto& [value, node] : nodes_) {
            res += fmt::format("{}: {}\n", value, node);
        }
        return res;
    }

private:
    std::unordered_map<LeftValue, InterfereNode> nodes_;
    TargetABI abi;
};

}  // namespace ir::lowering
