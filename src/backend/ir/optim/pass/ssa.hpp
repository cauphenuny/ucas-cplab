/// @brief Pass Constructing SSA IR

#include "backend/ir/ir.hpp"
#include "backend/ir/op.hpp"
#include "backend/ir/optim/cfg.hpp"
#include "backend/ir/optim/dataflow/dominance.hpp"
#include "backend/ir/optim/dataflow/framework.hpp"
#include "backend/ir/optim/dataflow/liveness.hpp"
#include "backend/ir/optim/dominance.hpp"
#include "framework.hpp"

#include <queue>
#include <unordered_set>

namespace ir::optim::pass {

namespace ssa {

struct AddPhi : Pass {
    void apply(Program& prog) override {
        for (auto& func : prog.getFuncs()) {
            transform(*func, prog);
        }
    }

private:
    auto definitions(const Func& func, const Alloc* alloc) {
        std::unordered_set<Block*> defs;
        auto val = alloc->value();

        /// FIXME: maybe not all def is MOV op?

        for (const auto& block : func.blocks()) {
            for (const auto& inst : block->insts()) {
                match(
                    inst,
                    [&](const UnaryInst& u) {
                        if (u.op == UnaryInstOp::MOV) {
                            match(
                                u.result,
                                [&](const NamedValue& n) {
                                    if (n == val) {
                                        defs.insert(block.get());
                                    }
                                },
                                [](const auto&) {});
                        }
                    },
                    [](const auto& i) {});
            }
        }
        return defs;
    }

    void transform(Func& func, Program& prog) {
        auto cfg = ControlFlowGraph(func);
        auto dom_flow = DataFlow<flow::Dominance>(cfg, prog);
        auto dom_tree = DominanceTree(dom_flow);
        auto dom_frontier = DominanceFrontier(cfg, dom_tree);
        auto live_vars = DataFlow<flow::Liveness>(cfg, prog);

        for (auto& alloc : func.locals()) {
            auto in_worklist = definitions(func, alloc.get());
            std::queue<Block*> worklist;
            for (auto block : in_worklist) worklist.push(block);
            std::unordered_map<Block*, bool> has_phi;
            std::unordered_map<Block*, bool> visited;

            auto val = alloc->value();

            while (worklist.size()) {
                auto block = worklist.front();
                worklist.pop();
                if (visited[block]) continue;
                visited[block] = true;
                for (auto frontier : dom_frontier.frontier(block)) {
                    if (!live_vars.in[frontier].contains(val))
                        continue;  // no need to insert phi if not live-in
                    if (!has_phi[frontier]) {
                        auto phi = PhiInst{.result = val};
                        for (auto pred : cfg.pred[frontier]) {
                            phi.args.emplace_back(pred, LeftValue{val});
                        }
                        frontier->prepend(Inst{std::move(phi)});
                        has_phi[frontier] = true;
                    }
                    if (!in_worklist.count(frontier)) {
                        worklist.push(frontier);
                        in_worklist.insert(frontier);
                    }
                }
            }
        }
    }
};

struct Rename : Pass {
    void apply(Program& prog) override {}
};

}  // namespace ssa

using ToSSA = Compose<ssa::AddPhi, ssa::Rename>;

}  // namespace ir::optim::pass