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
#include <vector>

namespace ir::lowering {

constexpr int LIVE_PRIORITY = -1;
constexpr int USEDEF_PRIORITY = 3;

struct InterfereNode {
    LeftValue value;
    std::unordered_set<LeftValue> interfere;
    std::unordered_set<LeftValue> move;
    size_t degree;
    std::optional<size_t> color;
    TO_STRING(InterfereNode, value, interfere, move, degree, color);
};

struct InterfereGraph {
    explicit InterfereGraph(size_t max_color) : max_color(max_color) {}

    LeftValue alias(const LeftValue& value) {
        if (!aliases_.count(value)) {
            aliases_[value] = value;
        } else if (!(aliases_[value] == value)) {
            aliases_[value] = alias(aliases_[value]);
        }
        return aliases_[value];
    }

    void set_alias(const LeftValue& dest, const LeftValue& src) {
        aliases_[src] = dest;
    }

    bool same(const LeftValue& a, const LeftValue& b) {
        return alias(a) == alias(b);
    }

    bool interferes(LeftValue a, LeftValue b) {
        a = alias(a), b = alias(b);
        if (a == b) return false;
        ensure(a), ensure(b);
        return nodes_[a].interfere.count(b) > 0;
    }

    bool move_related(LeftValue a, LeftValue b) {
        a = alias(a), b = alias(b);
        if (a == b) return false;
        ensure(a), ensure(b);
        return nodes_[a].move.count(b) > 0;
    }

    void pin(LeftValue value, size_t color) {
        value = alias(value);
        ensure(value);
        nodes_[value].color = color;
    }

    InterfereNode merge_preview(LeftValue dest, LeftValue src) {
        dest = alias(dest), src = alias(src);
        ensure(dest), ensure(src);
        if (dest == src) return nodes_.at(dest);
        InterfereNode merged{.value = dest};
        for (auto& [value, node] : nodes_) {
            if (value == dest) continue;
            if (node.interfere.count(src) || node.interfere.count(dest)) {
                merged.interfere.insert(value);
            }
            if (node.move.count(src) || node.move.count(dest)) {
                merged.move.insert(value);
            }
        }
        return merged;
    }

    bool mergable(LeftValue dest, LeftValue src) {
        dest = alias(dest), src = alias(src);
        if (dest == src) return false;
        ensure(dest), ensure(src);
        if (nodes_[src].color) return false;      // cannot merge pinned colored node
        if (interferes(dest, src)) return false;  // cannot merge interfering nodes
        auto george = [&] {
            for (const auto& neighbor : nodes_[src].interfere) {
                if (!(nodes_[neighbor].degree < max_color || interferes(dest, neighbor))) {
                    return false;
                }
            }
            return true;
        }();
        auto briggs = [&] {
            auto merged = merge_preview(dest, src);
            size_t cnt = 0;
            for (const auto& neighbor : merged.interfere) {
                cnt += nodes_[neighbor].degree >= max_color ? 1 : 0;
            }
            return cnt < max_color;
        }();
        return george || briggs;
    }

    InterfereNode& operator[](const LeftValue& value) {
        ensure(alias(value));
        return nodes_.at(alias(value));
    }

    static std::pair<InterfereGraph, InterfereGraph> from_precolored(const PrecolorVars& precolor,
                                                                     const TargetABI& abi) {
        InterfereGraph general(abi.reg.generals.size), floating(abi.reg.floats.size);
        for (const auto& [key, alloc] : precolor) {
            auto& [type, id] = key;
            auto& graph = is_fp(type) ? floating : general;
            graph.pin(alloc->value(), id);
        }
        auto conn = [&](InterfereGraph& graph) {
            for (auto& node : graph.nodes()) {
                for (auto& other : graph.nodes()) {
                    graph.interfere(node.first, other.first);
                }
            }
        };
        conn(general), conn(floating);
        return {general, floating};
    }

    static std::pair<std::vector<LeftValue*>, std::vector<LeftValue*>>
    divide(const std::vector<LeftValue*>& vars) {
        std::vector<LeftValue*> general, floating;
        for (auto var : vars) {
            if (is_fp(type_of(*var))) {
                floating.push_back(var);
            } else {
                general.push_back(var);
            }
        }
        return {general, floating};
    }

    static std::pair<InterfereGraph, InterfereGraph>
    build(Program& program, const PrecolorVars& precolored, const TargetABI& abi) {
        using namespace analysis;
        auto graphs = from_precolored(precolored, abi);
        auto graph_of = [&](const LeftValue& v) -> InterfereGraph& {
            return is_fp(type_of(v)) ? graphs.second : graphs.first;
        };

        for (auto& func : program.funcs()) {
            auto cfg = ControlFlowGraph(*func);
            auto liveness = DataFlow<flow::Liveness>(cfg, program);
            for (auto& block : func->blocks()) {

                auto block_liveout = liveness.out.at(block.get());
                if (auto exit_use = utils::used_var(block->exit())) {
                    block_liveout.insert(*exit_use);
                    graph_of(*exit_use).prior[*exit_use]++;
                }

                auto lives = std::move(block_liveout).divide([](const LeftValue& v) {
                    return !is_fp(type_of(v));
                });
                auto live_of = [&](const LeftValue& v) -> decltype(auto) {
                    return is_fp(type_of(v)) ? lives.second : lives.first;
                };

                for (auto inst_it = block->insts().rbegin(); inst_it != block->insts().rend();
                     ++inst_it) {
                    auto& inst = *inst_it;
                    for (auto var : utils::vars(inst)) graph_of(*var).prior[*var] += USEDEF_PRIORITY;

                    if (auto mov = std::get_if<UnaryInst>(&inst);
                        mov && mov->op == UnaryInstOp::MOV) {
                        if (auto src = std::get_if<LeftValue>(&mov->operand)) {
                            graph_of(*src).move(*mov->result, *src);
                        }
                    }

                    std::vector<LeftValue> defs;
                    if (auto def = utils::defined_var(inst)) {
                        defs.push_back(*def);
                    }
                    if (std::holds_alternative<CallInst>(inst)) {
                        auto retaddr =
                            precolored.at({type::integer(), abi.reg.return_addr})->value();
                        defs.emplace_back(retaddr);
                    }

                    for (const auto& d : defs) {
                        for (const auto& l : live_of(d)) {
                            graph_of(d).interfere(d, l);
                        }
                    }

                    for (const auto& l : lives.first) graph_of(l).prior[l] += LIVE_PRIORITY;
                    for (const auto& l : lives.second) graph_of(l).prior[l] += LIVE_PRIORITY;

                    // forbid live variables cross call-inst to be colored as caller-saved registers
                    if (std::holds_alternative<CallInst>(inst)) {
                        for (auto& l : lives.first) {
                            for (auto reg : abi.reg.generals.caller_saved) {
                                graph_of(l).interfere(
                                    l, precolored.at({type::integer(), reg})->value());
                            }
                        }
                        for (auto& l : lives.second) {
                            for (auto reg : abi.reg.floats.caller_saved) {
                                graph_of(l).interfere(
                                    l, precolored.at({type::floating(), reg})->value());
                            }
                        }
                    }

                    for (const auto& def : defs) live_of(def).erase(def);

                    // phi-inst's uses is added by block-wise liveness analysis
                    if (std::holds_alternative<PhiInst>(inst)) continue;

                    for (const auto& use : utils::used_vars(inst)) live_of(*use).insert(*use);
                }
            }
        }

        auto reserve = [&](InterfereGraph& graph, const Type& type, const RegisterABI& abi) {
            for (const auto& node : graph.nodes()) {
                for (auto reserved : abi.reserved) {
                    graph.interfere(node.first, precolored.at({type, reserved})->value());
                }
            }
        };
        reserve(graphs.first, type::integer(), abi.reg.generals);
        reserve(graphs.second, type::floating(), abi.reg.floats);

        return graphs;
    }
    [[nodiscard]] const std::unordered_map<LeftValue, InterfereNode>& nodes() const {
        return nodes_;
    }

    TO_STRING(InterfereGraph, nodes_);

    const size_t max_color;

    void interfere(const LeftValue& u, const LeftValue& v) {
        if (!need_register(u) || !need_register(v)) return;
        auto u_ = alias(u), v_ = alias(v);
        if (u_ == v_) return;
        ensure(u_), ensure(v_);
        if (!nodes_[u_].interfere.count(v_)) {
            nodes_[u_].interfere.insert(v_), nodes_[u_].degree++;
        }
        if (!nodes_[v_].interfere.count(u_)) {
            nodes_[v_].interfere.insert(u_), nodes_[v_].degree++;
        }
    }

    double priority(const LeftValue& value) {
        return static_cast<double>(prior[value]) / nodes_[value].degree;
    }

private:
    std::unordered_map<LeftValue, LeftValue> aliases_;
    std::unordered_map<LeftValue, InterfereNode> nodes_;
    std::unordered_map<LeftValue, int> prior;

    void ensure(const LeftValue& value) {
        if (nodes_.count(value) == 0) {
            nodes_[value] = InterfereNode{.value = value};
        }
    }

    void move(const LeftValue& u, const LeftValue& v) {
        if (!need_register(u) || !need_register(v)) return;
        auto u_ = alias(u), v_ = alias(v);
        if (u_ == v_) return;
        ensure(u_), ensure(v_);
        nodes_[u_].move.insert(v_);
        nodes_[v_].move.insert(u_);
    }

    bool need_register(const LeftValue& v) {
        if (auto named = std::get_if<NamedValue>(&v)) {
            if (auto alloc = std::get_if<const Alloc*>(&named->def)) {
                return !(*alloc)->reference;
            }
        }
        return true;
    };
};

}  // namespace ir::lowering
