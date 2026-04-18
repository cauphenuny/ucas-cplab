#pragma once
#include "backend/ir/analysis/cfg.hpp"
#include "backend/ir/analysis/dataflow/dominance.hpp"
#include "backend/ir/analysis/dataflow/framework.hpp"
#include "backend/ir/ir.hpp"

#include <unordered_map>
#include <vector>

namespace ir::analysis {

struct DominanceTree {
    auto idom(Block* blk) const -> Block* {
        auto it = idom_map.find(blk);
        if (it == idom_map.end()) {
            throw COMPILER_ERROR(fmt::format("Block {} not found in dominance tree", blk->label));
        }
        return it->second;
    }

    auto children(Block* blk) const -> const std::vector<Block*>& {
        static const std::vector<Block*> empty;
        auto it = children_map.find(blk);
        return it != children_map.end() ? it->second : empty;
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

    DominanceTree(const DataFlow<flow::Dominance>& dom_flow) {
        auto& func = dom_flow.cfg.func;
        auto& dom = dom_flow.out;
        for (const auto& block_box : func.blocks()) {
            auto block = block_box.get();
            if (block == func.entrance())
                idom_map[block] = nullptr;  // entry block has no dominator
            Block* idom_block = nullptr;
            for (auto dom_block : dom.at(block)) {
                if (dom_block == block) continue;
                if (!idom_block || dom.at(dom_block).size() > dom.at(idom_block).size()) {
                    idom_block = dom_block;
                }
            }
            idom_map[block] = idom_block;
        }
        for (auto& [child, parent] : idom_map) {
            if (parent) children_map[parent].push_back(child);
        }
    }

private:
    std::unordered_map<Block*, Block*> idom_map;  // immediate dominator
    std::unordered_map<Block*, std::vector<Block*>> children_map;
};

struct DominanceFrontier {
    static auto print(Block* blk) -> std::string {
        return blk->label;
    }
    using Data = Set<Block*, print>;

    auto frontier(Block* blk) const -> const Data& {
        auto it = frontier_map.find(blk);
        if (it == frontier_map.end()) {
            throw COMPILER_ERROR(
                fmt::format("Block {} not found in dominance frontier", blk->label));
        }
        return it->second;
    }

    /// NOTE: frontier(N) = { W | N dom a pred of W, and N does not dom W }
    DominanceFrontier(const ControlFlowGraph& cfg, const DominanceTree& dom_tree) {
        for (auto& block_box : cfg.func.blocks()) frontier_map[block_box.get()] = Data::empty();

        for (auto& block_box : cfg.func.blocks()) {
            Block* block = block_box.get();
            auto idom = dom_tree.idom(block);
            if (!idom) continue;  // skip entry block
            for (auto pred : cfg.pred.at(block)) {
                auto runner = pred;
                while (runner && runner != idom) {
                    /// NOTE: runner dominates pred, and do not dominates block
                    frontier_map[runner].insert(block);
                    runner = dom_tree.idom(runner);
                }
            }
        }
    }

private:
    std::unordered_map<Block*, Data> frontier_map;
};

}  // namespace ir::analysis