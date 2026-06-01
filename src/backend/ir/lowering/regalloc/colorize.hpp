/// @brief Chaitin-Briggs Graph Coloring Register Allocator

#pragma once
#include "backend/ir/ir.h"
#include "backend/ir/lowering/regalloc/graph.hpp"
#include "backend/ir/lowering/regalloc/precolorize.hpp"

#include <list>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ir::lowering {

struct ColorizeResult {
    std::vector<LeftValue> spilled;
    ColorMap colors;
};

struct BriggsAllocator {
    struct {
        std::list<LeftValue> precolored;
        std::list<LeftValue> initial;

        std::list<LeftValue> simplify;
        std::list<LeftValue> freeze;
        std::list<LeftValue> to_spill;

        std::list<LeftValue> spilled;
        std::list<LeftValue> coalesced;
        std::list<LeftValue> colored;

        std::list<LeftValue> select_stack;
    } nodes;

    using NodeLocation = std::pair<std::list<LeftValue>*, std::list<LeftValue>::iterator>;
    std::unordered_map<LeftValue, NodeLocation> node_locations;

    using Move = std::pair<LeftValue, LeftValue>;
    struct {
        std::list<Move> coalesced;
        std::list<Move> constrained;
        std::list<Move> frozen;
        std::list<Move> ready;
        std::list<Move> scheduled;
    } moves;
    using MoveLocation = std::pair<std::list<Move>*, std::list<Move>::iterator>;
    std::unordered_map<Move, MoveLocation> move_locations;

    InterfereGraph graph;

    ColorizeResult result;

    template <typename T>
    void relocate(const T& value, std::list<T>& to,
                  std::unordered_map<T, std::pair<std::list<T>*, typename std::list<T>::iterator>>&
                      locations) {
        auto& [from_list, from_it] = locations.at(value);
        from_list->erase(from_it);
        to.push_back(value);
        locations[value] = {&to, std::prev(to.end())};
    }

    BriggsAllocator(InterfereGraph graph) : graph(std::move(graph)) {
        for (const auto& [value, node] : this->graph.nodes()) {
            if (node.color) {
                nodes.precolored.push_back(value);
                node_locations[value] = {&nodes.precolored, std::prev(nodes.precolored.end())};
                result.colors[value] = node.color.value();
            } else {
                nodes.initial.push_back(value);
                node_locations[value] = {&nodes.initial, std::prev(nodes.initial.end())};
            }
            for (auto& move_src : node.move) {
                auto mov = std::make_pair(value, move_src);
                moves.ready.push_back(mov);
                move_locations[mov] = {&moves.ready, std::prev(moves.ready.end())};
            }
        }
    }

    auto adjacent(const LeftValue& value) {
        std::vector<LeftValue> result;
        for (auto& adj : graph[value].interfere) {
            auto loc = node_locations.at(adj).first;
            if (loc == &nodes.select_stack || loc == &nodes.coalesced) continue;
            result.push_back(adj);
        }
        return result;
    }

    auto effective_moves(const LeftValue& value) {
        std::vector<Move> result;
        for (auto& move_src : graph[value].move) {
            auto mov = std::make_pair(value, move_src);
            auto loc = move_locations.at(mov).first;
            if (loc == &moves.scheduled || loc == &moves.ready) {
                result.push_back(mov);
            }
        }
        return result;
    }

    auto move_related(const LeftValue& value) {
        return !effective_moves(value).empty();
    }

    void initialize() {
        for (auto it = nodes.initial.begin(); it != nodes.initial.end();) {
            const auto& val = *it;
            auto next = std::next(it);
            if (graph[val].degree >= graph.max_color)
                relocate(val, nodes.to_spill, node_locations);
            else if (move_related(val))
                relocate(val, nodes.freeze, node_locations);
            else
                relocate(val, nodes.simplify, node_locations);
            it = next;
        }
    }

    void decrement_degree(const LeftValue& value) {
        if (node_locations.at(value).first == &nodes.precolored) return;
        auto d = graph[value].degree;
        graph[value].degree = d - 1;
        if (d == graph.max_color) {
            enable_moves(value);
            for (const auto& adj : adjacent(value)) {
                enable_moves(adj);
            }
            if (move_related(value))
                relocate(value, nodes.freeze, node_locations);
            else
                relocate(value, nodes.simplify, node_locations);
        }
    }

    void enable_moves(const LeftValue& dest) {
        for (const auto& mov : effective_moves(dest)) {
            if (move_locations[mov].first == &moves.scheduled) {
                relocate(mov, moves.ready, move_locations);
            }
        }
    }

    void simplify() {
        auto n = nodes.simplify.front();
        relocate(n, nodes.select_stack, node_locations);
        for (const auto& neighbor : adjacent(n)) {
            decrement_degree(neighbor);
        }
    }

    void merge(const LeftValue& dest, const LeftValue& src) {
        relocate(src, nodes.coalesced, node_locations);
        graph[dest].move.insert(graph[src].move.begin(), graph[src].move.end());
        enable_moves(src);
        for (const auto& neighbor : adjacent(src)) {
            graph.interfere(dest, neighbor);
            decrement_degree(neighbor);
        }
        if (graph[dest].degree >= graph.max_color && node_locations[dest].first == &nodes.freeze) {
            relocate(dest, nodes.to_spill, node_locations);
        }
        graph.set_alias(dest, src);
    }

    void freeze() {
        auto f = nodes.freeze.front();
        relocate(f, nodes.simplify, node_locations);
        freeze_moves(f);
    }

    void freeze_moves(const LeftValue& dest) {
        for (const auto& mov : effective_moves(dest)) {
            relocate(mov, moves.frozen, move_locations);
            auto [x, y] = mov;
            auto src = graph.alias(x) == graph.alias(dest) ? graph.alias(y) : graph.alias(x);
            if (effective_moves(src).empty() && graph[src].degree < graph.max_color &&
                node_locations[src].first == &nodes.freeze) {
                relocate(src, nodes.simplify, node_locations);
            }
        }
    }

    void unfreeze(const LeftValue& value) {
        auto location = node_locations.at(value).first;
        if (location == &nodes.precolored || move_related(value) ||
            graph[value].degree >= graph.max_color)
            return;
        relocate(value, nodes.simplify, node_locations);
    }

    void coalesce() {
        auto mov = moves.ready.front();
        auto [x, y] = mov;
        x = graph.alias(x), y = graph.alias(y);
        if (node_locations[y].first == &nodes.precolored) {
            swap(x, y);
        }
        if (x == y) {
            relocate(mov, moves.coalesced, move_locations);
            unfreeze(x);
        } else if (node_locations[y].first == &nodes.precolored || graph.interferes(x, y)) {
            relocate(mov, moves.constrained, move_locations);
            unfreeze(x), unfreeze(y);
        } else if (graph.mergable(x, y)) {
            merge(x, y);
            unfreeze(x);
            relocate(mov, moves.coalesced, move_locations);
        } else {
            relocate(mov, moves.scheduled, move_locations);
        }
    }

    void spill() {
        auto m = nodes.to_spill.front();  // TODO: heuristic
        relocate(m, nodes.simplify, node_locations);
        freeze_moves(m);
    }

    void assign_colors() {
        while (!nodes.select_stack.empty()) {
            auto n = nodes.select_stack.back();
            std::unordered_set<size_t> ok_colors;
            for (size_t c = 0; c < graph.max_color; c++) ok_colors.insert(c);
            for (const auto& neighbor : graph[n].interfere) {
                auto alias = graph.alias(neighbor);
                if (result.colors.count(alias)) {
                    ok_colors.erase(result.colors.at(alias));
                }
            }
            if (ok_colors.empty()) {
                relocate(n, nodes.spilled, node_locations);
            } else {
                result.colors[n] = *ok_colors.begin();
                relocate(n, nodes.colored, node_locations);
            }
        }
        for (auto& n : nodes.coalesced) {
            result.colors[n] = result.colors.at(graph.alias(n));
        }
    }

    ColorizeResult colorize() && {
        initialize();
        while (nodes.simplify.size() || nodes.freeze.size() || nodes.to_spill.size() ||
               moves.ready.size()) {
            if (nodes.simplify.size())
                simplify();
            else if (moves.ready.size())
                coalesce();
            else if (nodes.freeze.size())
                freeze();
            else if (nodes.to_spill.size())
                spill();
        }
        assign_colors();
        for (auto& s : nodes.spilled) {
            result.spilled.push_back(s);
        }
        return result;
    }
};

}  // namespace ir::lowering