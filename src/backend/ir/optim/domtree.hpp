#include "backend/ir/ir.hpp"
#include "backend/ir/optim/cfg.hpp"
#include "backend/ir/optim/dataflow.hpp"

#include <unordered_map>

namespace ir::optim {

struct DominanceTree {
    auto idom(const Block* blk) const -> const Block* {
        auto it = idom_map.find(blk);
        if (it == idom_map.end()) {
            throw COMPILER_ERROR(fmt::format("Block {} not found in dominance tree", blk->label));
        }
        return it->second;
    }

    // clang-format off

    /// NOTE: the idom is the dominator which has the largest dominator set (closest to the block)

    // proof:
    //   - lemma: if u dom w and v dom w, then u dom v or v dom u.
    //     proof: if not, then there exists a path from entry to w that goes through u but not v, contradicts with v dom w.
    //   - lemma: any differenct dominator do not has same-sized dominator set.
    //     proof: consider u dom w, v dom w, then u dom v or v dom u, contradicts with |dom(u)| == |dom(v)|
    //   since each dominator has different-sized dominator set, the immediate dominator must be the one with largest dominator set.

    // clang-format on

    DominanceTree(const DataFlow<flows::Dominance>& dom_flow) {
        auto& func = dom_flow.cfg.func;
        auto& dom = dom_flow.out;
        for (const auto& block_box : func.blocks()) {
            auto block = block_box.get();
            if (block == func.entrance())
                idom_map[block] = nullptr;  // entry block has no dominator
            const Block* idom_block = nullptr;
            for (auto dom_block : dom.at(block)) {
                if (dom_block == block) continue;
                if (!idom_block || dom.at(dom_block).size() > dom.at(idom_block).size()) {
                    idom_block = dom_block;
                }
            }
            idom_map[block] = idom_block;
        }
    }

private:
    std::unordered_map<const Block*, const Block*> idom_map;  // immediate dominator
};

}  // namespace ir::optim