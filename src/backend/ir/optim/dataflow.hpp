#pragma once

#include "backend/ir/ir.hpp"
#include "backend/ir/optim/cfg.hpp"

#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace ir::optim {

// clang-format off

/// Unified Data Flow Equation Solver
///
/// FlowTrait:
///   using Data = ...;                              // Data type of Data Flow, requires operator==
///   static constexpr bool is_forward = true/false; // forward or backward analysis
///   static Data boundary();                        // initval of entry/exit nodes
///   static Data top();                             // initval of other nodes (identity element of meet)
///   static Data meet(const Data& a, const Data& b);
///   static Data transfer(const Block& blk, const Data& in);
///

// clang-format on

template <typename T, typename = void> struct is_flow_trait : std::false_type {};

template <typename T>
struct is_flow_trait<T, std::void_t<typename T::Data, decltype(T::is_forward),
                                    decltype(std::declval<const typename T::Data&>() ==
                                             std::declval<const typename T::Data&>()),
                                    decltype(T::boundary()), decltype(T::top()),
                                    decltype(T::meet(std::declval<const typename T::Data&>(),
                                                     std::declval<const typename T::Data&>())),
                                    decltype(T::transfer(std::declval<const Block&>(),
                                                         std::declval<const typename T::Data&>()))>>
    : std::true_type {};

template <typename T> inline constexpr bool is_flow_trait_v = is_flow_trait<T>::value;

template <typename Trait, typename = std::enable_if_t<is_flow_trait_v<Trait>>> struct DataFlow {
    using Data = typename Trait::Data;
    static constexpr bool is_forward = Trait::is_forward;

    const ControlFlowGraph& cfg;
    std::unordered_map<const Block*, Data> in;
    std::unordered_map<const Block*, Data> out;

    explicit DataFlow(const ControlFlowGraph& cfg) : cfg(cfg) {
        solve();
    }

private:
    void solve() {
        auto& blocks = cfg.func.blocks();

        // backward analysis is forward analysis in reversed graph
        const auto& flow_pred = is_forward ? cfg.pred : cfg.succ;
        auto& flow_in = is_forward ? in : out;
        auto& flow_out = is_forward ? out : in;

        // determine if a block is a boundary (entry for forward, exit for backward)
        auto is_boundary = [&](const Block* blk) {
            if constexpr (is_forward) {
                return blk == blocks.front().get();
            } else {
                return std::holds_alternative<ReturnExit>(blk->exit());
            }
        };

        // init
        for (auto& blk_ptr : blocks) {
            const Block* blk = blk_ptr.get();
            if (is_boundary(blk)) {
                flow_in[blk] = Trait::boundary();
                flow_out[blk] = Trait::transfer(*blk, flow_in[blk]);
            } else {
                flow_in[blk] = Trait::top();
                flow_out[blk] = Trait::top();
            }
        }

        // Iterate until convergence
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto& blk_ptr : blocks) {
                const Block* blk = blk_ptr.get();

                // flow_in[B] = ⊓ flow_out[P], P ∈ flow_pred[B]
                Data new_in = Trait::top();
                if (auto it = flow_pred.find(blk); it != flow_pred.end()) {
                    for (const Block* p : it->second) {
                        new_in = Trait::meet(new_in, flow_out[p]);
                    }
                }
                if (is_boundary(blk)) {
                    new_in = Trait::meet(new_in, Trait::boundary());
                }
                flow_in[blk] = std::move(new_in);

                // flow_out[B] = transfer(B, flow_in[B])
                Data new_out = Trait::transfer(*blk, flow_in[blk]);
                if (!(new_out == flow_out[blk])) {
                    flow_out[blk] = std::move(new_out);
                    changed = true;
                }
            }
        }
    }
};

namespace flows {

template <typename T> struct Set {
    bool is_universe = true;
    std::unordered_set<T> set;

    bool operator==(const Set& other) const {
        if (is_universe != other.is_universe) return false;
        if (is_universe) return true;
        return set == other.set;
    }

    static Set universe() {
        return {true, {}};
    }
    static Set empty() {
        return {false, {}};
    }
    static Set intersection_set(const Set& a, const Set& b) {
        if (a.is_universe) return b;
        if (b.is_universe) return a;
        Set res{false, {}};
        // iterate over smaller set
        auto it_set = a.set.size() < b.set.size() ? &a.set : &b.set;
        auto ref_set = a.set.size() < b.set.size() ? &b.set : &a.set;
        for (const auto& elem : *it_set) {
            if (ref_set->count(elem)) res.set.insert(elem);
        }
        return res;
    }
    static Set union_set(const Set& a, const Set& b) {
        if (a.is_universe || b.is_universe) return universe();
        Set res{false, a.set};
        res.set.insert(b.set.begin(), b.set.end());
        return res;
    }

    [[nodiscard]] auto begin() const {
        if (is_universe) {
            throw COMPILER_ERROR("Cannot iterate over universe set");
        }
        return set.begin();
    }
    [[nodiscard]] auto end() const {
        if (is_universe) {
            throw COMPILER_ERROR("Cannot iterate over universe set");
        }
        return set.end();
    }

    [[nodiscard]] auto size() const {
        if (is_universe) {
            throw COMPILER_ERROR("Cannot get size of universe set");
        }
        return set.size();
    }
};

struct Dominance {
    static constexpr bool is_forward = true;
    using Data = Set<const Block*>;

    static constexpr auto boundary = Data::empty;
    static constexpr auto top = Data::universe;
    static constexpr auto meet = Data::intersection_set;

    static Data transfer(const Block& blk, const Data& in) {
        if (in.is_universe) return in;
        Data res = in;
        res.set.insert(&blk);
        return res;
    }
};

static_assert(is_flow_trait_v<Dominance>);

}  // namespace flows

}  // namespace ir::optim