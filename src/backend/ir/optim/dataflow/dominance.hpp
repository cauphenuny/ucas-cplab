#pragma once

#include "framework.hpp"

namespace ir::optim::flow {

struct Dominance {
    static constexpr bool is_forward = true;
    static auto print(const Block* blk) -> std::string {
        return blk->label;
    }
    using Data = Set<const Block*, print>;

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

static_assert(!has_context<Dominance>::value);
static_assert(is_flow_trait_v<Dominance>);

}  // namespace ir::optim::flow