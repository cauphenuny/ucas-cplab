#include "framework.hpp"

namespace ir::optim::flow {

struct Dominance {
    static constexpr bool is_forward = true;
    using Data = Set<const Block*>;
    using Context = void*;

    static constexpr auto boundary = Data::empty;
    static constexpr auto top = Data::universe;
    static constexpr auto meet = Data::intersection_set;

    static Data transfer(const Block& blk, const Data& in, Context&) {
        if (in.is_universe) return in;
        Data res = in;
        res.set.insert(&blk);
        return res;
    }
};

static_assert(is_flow_trait_v<Dominance>);

}  // namespace ir::optim::flow