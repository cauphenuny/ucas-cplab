#pragma once

#include "backend/ir/ir.hpp"
#include "backend/ir/optim/cfg.hpp"
#include "utils/serialize.hpp"

#include <set>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace ir::optim {

// clang-format off

/// Unified Data Flow Equation Solver
///
/// FlowTrait:
///   using Data = ...;                              // Data type of Data Flow, requires operator==
///   using Context = ...;                           // optional context type for transfer function
///   static constexpr bool is_forward = true/false; // forward or backward analysis
///   static Data boundary(Context& ctx);            // initval of entry/exit nodes, ctx is optional
///   static Data top();                             // initval of other nodes (identity element of meet)
///   static Data meet(const Data& a, const Data& b);
///   static Data transfer(Block& blk, const Data& in, Context& ctx);  // transfer function, context is optional
///   static Context init(const ControlFlowGraph& cfg);  // optional context initializer
///

// clang-format on

template <typename T, typename = void> struct has_context : std::false_type {};
template <typename T>
struct has_context<
    T, std::void_t<typename T::Context, decltype(T::init(std::declval<const ControlFlowGraph&>(),
                                                         std::declval<const Program&>()))>>
    : std::true_type {};

template <typename T, typename = void> struct is_flow_trait : std::false_type {};

template <typename T>
struct is_flow_trait<T, std::void_t<typename T::Data, decltype(T::is_forward),
                                    decltype(std::declval<const typename T::Data&>() ==
                                             std::declval<const typename T::Data&>()),
                                    decltype(T::top()),
                                    decltype(T::meet(std::declval<const typename T::Data&>(),
                                                     std::declval<const typename T::Data&>()))>>
    : std::true_type {};

template <typename T> inline constexpr bool is_flow_trait_v = is_flow_trait<T>::value;

template <typename Trait, bool HasContext = has_context<Trait>::value> struct DataFlowContext {
    using Context = typename Trait::Context;
    Context ctx;
    DataFlowContext(const ControlFlowGraph& cfg, const Program& prog)
        : ctx(Trait::init(cfg, prog)) {}
};

template <typename Trait> struct DataFlowContext<Trait, false> {
    DataFlowContext(const ControlFlowGraph&, const Program&) {}
};

template <typename Trait, typename = std::enable_if_t<is_flow_trait_v<Trait>>>
struct DataFlow : private DataFlowContext<Trait> {
    using Data = typename Trait::Data;
    static constexpr bool is_forward = Trait::is_forward;

    const ControlFlowGraph& cfg;
    std::unordered_map<Block*, Data> in;
    std::unordered_map<Block*, Data> out;

    DataFlow(const ControlFlowGraph& cfg, const Program& prog)
        : DataFlowContext<Trait>(cfg, prog), cfg(cfg) {
        solve();
    }

    [[nodiscard]] auto toString() const -> std::string {
        std::string res, ind = fmt_indent::state.indent();
        for (const auto& [blk, data] : in) {
            res += fmt::format("{}{}:\n", ind, blk->label);
            res += fmt::format("{}  in: {}\n", ind, data);
            res += fmt::format("{}  out: {}\n", ind, out.at(blk));
        }
        return res;
    }

private:
    auto transfer(Block& blk, const Data& in) {
        if constexpr (has_context<Trait>::value) {
            return Trait::transfer(blk, in, this->ctx);
        } else {
            return Trait::transfer(blk, in);
        }
    }

    auto boundary() {
        if constexpr (has_context<Trait>::value) {
            return Trait::boundary(this->ctx);
        } else {
            return Trait::boundary();
        }
    }

    void solve() {
        auto& blocks = cfg.func.blocks();

        // backward analysis is forward analysis in reversed graph
        const auto& flow_pred = is_forward ? cfg.pred : cfg.succ;
        auto& flow_in = is_forward ? in : out;
        auto& flow_out = is_forward ? out : in;

        // determine if a block is a boundary (entry for forward, exit for backward)
        auto is_boundary = [&](Block* blk) {
            if constexpr (is_forward) {
                return blk == blocks.front().get();
            } else {
                return std::holds_alternative<ReturnExit>(blk->exit());
            }
        };

        // init
        for (auto& blk_ptr : blocks) {
            Block* blk = blk_ptr.get();
            if (is_boundary(blk)) {
                flow_in[blk] = this->boundary();
                flow_out[blk] = this->transfer(*blk, flow_in[blk]);
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
                Block* blk = blk_ptr.get();

                // flow_in[B] = ⊓ flow_out[P], P ∈ flow_pred[B]
                Data new_in = Trait::top();
                if (auto it = flow_pred.find(blk); it != flow_pred.end()) {
                    for (Block* p : it->second) {
                        new_in = Trait::meet(new_in, flow_out[p]);
                    }
                }
                if (is_boundary(blk)) {
                    new_in = Trait::meet(new_in, this->boundary());
                }
                flow_in[blk] = std::move(new_in);

                // flow_out[B] = transfer(B, flow_in[B])
                Data new_out = this->transfer(*blk, flow_in[blk]);
                if (!(new_out == flow_out[blk])) {
                    flow_out[blk] = std::move(new_out);
                    changed = true;
                }
            }
        }
    }
};

template <typename T, auto print> struct Set {
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

    [[nodiscard]] Set difference(const Set& other) const {
        if (is_universe && other.is_universe) return empty();
        if (is_universe && other.set.empty()) return *this;
        if (is_universe) {
            throw COMPILER_ERROR("Cannot difference non-trivial set from universe set");
        }
        if (other.is_universe) return empty();
        Set res{false, {}};
        for (const auto& elem : set) {
            if (!other.set.count(elem)) res.set.insert(elem);
        }
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

    [[nodiscard]] bool contains(const T& elem) const {
        if (is_universe) return true;
        return set.count(elem) > 0;
    }

    void insert(const T& elem) {
        if (is_universe) return;
        set.insert(elem);
    }
    void erase(const T& elem) {
        if (is_universe) return;
        set.erase(elem);
    }

    [[nodiscard]] auto toString() const -> std::string {
        if (is_universe) return "<universe>";
        std::string res = "{";
        bool first = true;
        std::set<std::string> elems;
        for (const auto& elem : set) {
            elems.insert(print(elem));
        }
        for (const auto& elem : elems) {
            if (!first) res += ", ";
            res += elem;
            first = false;
        }
        res += "}";
        return res;
    }
};

}  // namespace ir::optim