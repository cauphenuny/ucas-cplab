/// @brief Chaitin-Briggs Graph Coloring Register Allocator

#pragma once
#include "backend/ir/ir.h"
#include "backend/ir/lowering/regalloc/graph.hpp"
#include "backend/ir/lowering/regalloc/precolorize.hpp"

#include <cstdio>
#include <functional>
#include <list>
#include <optional>
#include <set>
#include <unordered_map>
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

        TO_STRING(Nodes, precolored, initial, simplify, freeze, to_spill, spilled, coalesced,
                  colored, select_stack);
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

        TO_STRING(Moves, coalesced, constrained, frozen, ready, scheduled);
    } moves;
    using MoveLocation = std::pair<std::list<Move>*, std::list<Move>::iterator>;
    std::unordered_map<Move, MoveLocation> move_locations;

    InterfereGraph graph;

    ColorizeResult result;

    std::function<int(size_t)> priority;

    template <typename T>
    void relocate(T value, std::list<T>& to,
                  std::unordered_map<T, std::pair<std::list<T>*, typename std::list<T>::iterator>>&
                      locations) {
        auto& [from_list, from_it] = locations.at(value);
        from_list->erase(from_it);
        to.push_back(value);
        locations[std::move(value)] = {&to, std::prev(to.end())};
    }

    BriggsAllocator(
        InterfereGraph graph, std::function<int(size_t)> priority = [](size_t) { return 0; })
        : graph(std::move(graph)), priority(std::move(priority)) {
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

    auto effective_moves(const LeftValue& dest) {
        std::vector<Move> result;
        for (auto& src : graph[dest].move) {
            auto mov = std::make_pair(dest, src);
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
        // fmt::println(stderr, "enabling moves for {}", dest);
        for (const auto& mov : effective_moves(dest)) {
            if (move_locations[mov].first == &moves.scheduled) {
                relocate(mov, moves.ready, move_locations);
            }
        }
    }

    void simplify() {
        auto n = nodes.simplify.front();
        // fmt::println(stderr, "simplifying node {}", n);
        relocate(n, nodes.select_stack, node_locations);
        for (const auto& neighbor : adjacent(n)) {
            decrement_degree(neighbor);
        }
    }

    void merge(const LeftValue& dest, const LeftValue& src) {
        // fmt::println(stderr, "merging {} into {}", src, dest);
        relocate(src, nodes.coalesced, node_locations);
        enable_moves(src);
        auto replace = [&](const Move& old_mov, std::optional<Move> new_mov) {
            auto [list, it] = move_locations.at(old_mov);
            move_locations.erase(old_mov), list->erase(it);
            if (new_mov)
                list->push_back(*new_mov),
                    move_locations[*new_mov] = {list, std::prev(list->end())};
        };
        for (const auto& m : graph[src].move) {
            if (graph[dest].move.count(m)) continue;
            graph[m].move.erase(src);
            if (!(m == dest)) graph[m].move.insert(dest);
            replace({src, m}, m == dest ? std::nullopt : std::make_optional<Move>(Move{dest, m}));
            replace({m, src}, m == dest ? std::nullopt : std::make_optional<Move>(Move{m, dest}));
        }
        graph[dest].move.insert(graph[src].move.begin(), graph[src].move.end());
        graph[dest].move.erase(dest);
        graph[dest].move.erase(src);
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
        // fmt::println(stderr, "freezing node {}", nodes.freeze.front());
        auto f = nodes.freeze.front();
        relocate(f, nodes.simplify, node_locations);
        freeze_moves(f);
    }

    void freeze_moves(const LeftValue& dest) {
        // fmt::println(stderr, "freezing moves for {}", dest);
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
        // fmt::println(stderr, "unfreezing {}", value);
        relocate(value, nodes.simplify, node_locations);
    }

    void coalesce() {
        auto mov = moves.ready.front();
        auto [x, y] = mov;
        // fmt::println(stderr, "coalescing {} and {}", x, y);
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
            relocate(mov, moves.coalesced, move_locations);
            merge(x, y);
            unfreeze(x);
        } else {
            relocate(mov, moves.scheduled, move_locations);
        }
    }

    void spill() {
        auto m = nodes.to_spill.front();
        // fmt::println(stderr, "spilling {}", m);
        for (const auto& n : nodes.to_spill) {
            // fmt::println(stderr, "spill candidate {} with priority {}, current best: {}/{}", n,
            // graph.priority(n), m, graph.priority(m));
            if (graph.priority(n) < graph.priority(m)) m = n;
        }
        relocate(m, nodes.simplify, node_locations);
        freeze_moves(m);
    }

    void assign_colors() {
        while (!nodes.select_stack.empty()) {
            auto n = nodes.select_stack.back();
            std::set<size_t> ok_colors;
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
                ssize_t best = -1;
                for (const auto& move : graph[n].move) {
                    auto alias = graph.alias(move);
                    if (result.colors.count(alias) && ok_colors.count(result.colors.at(alias))) {
                        best = result.colors.at(alias);
                    }
                }
                if (best == -1) {
                    for (const auto& c : ok_colors) {
                        if (best == -1 || priority(c) > priority(best)) best = c;
                    }
                }
                // fmt::println(stderr, "assigning color {} to {}", best, n);
                result.colors[n] = best;
                relocate(n, nodes.colored, node_locations);
            }
        }
        for (auto& n : nodes.coalesced) {
            auto a = graph.alias(n);
            if (result.colors.count(a)) {
                result.colors[n] = result.colors.at(a);
            } else {
                result.spilled.push_back(n);
            }
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
            // fmt::println("nodes: {}", nodes);
            // fmt::println("moves: {}\n", moves);
        }
        assign_colors();
        for (auto& s : nodes.spilled) {
            result.spilled.push_back(s);
        }
        return result;
    }
};

}  // namespace ir::lowering