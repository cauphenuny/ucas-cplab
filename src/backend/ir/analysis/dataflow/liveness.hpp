/// @brief live variable analysis
#pragma once

#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.hpp"
#include "framework.hpp"
#include "utils/match.hpp"

namespace ir::analysis::flow {

struct Liveness {
    static constexpr bool is_forward = false;
    using ElemType = LeftValue;
    static std::string print(const ElemType& elem) {
        return match(elem, [](const auto& val) { return fmt::format("{}", val); });
    }

    using Data = Set<ElemType, print>;
    struct Context {
        std::unordered_map<Block*, Data> gen, kill;
        Data global_variables;
        std::unordered_map<Block*, std::unordered_map<Block*, Data>> phi_uses;
    };

    static Data boundary(Context& ctx) {
        return ctx.global_variables;
    }
    static constexpr auto top = Data::empty;
    static constexpr auto meet = Data::union_set;

    static Data transfer(Block& blk, const Data& out, Context& ctx) {
        return Data::union_set(out.difference(ctx.kill.at(&blk)), ctx.gen.at(&blk));
    }

    static Data edge_transfer(Block* src, Block* dst, const Data& dst_in, Context& ctx) {
        if (ctx.phi_uses.count(dst) && ctx.phi_uses.at(dst).count(src)) {
            return Data::union_set(dst_in, ctx.phi_uses.at(dst).at(src));
        }
        return dst_in;
    }

    static Context init(const ControlFlowGraph& cfg, const Program& prog) {
        Context ctx;
        // TODO
        for (auto& block_box : cfg.func.blocks()) {
            auto gen = Data::empty(), kill = Data::empty();
            auto block = block_box.get();
            for (auto& inst : block->insts()) {
                if (auto phi = std::get_if<PhiInst>(&inst); phi) {
                    for (auto& [pred, val] : phi->args) {
                        if (auto var = utils::as_var(val); var) {
                            ctx.phi_uses[block][pred].insert(*var);
                        }
                    }
                }
                for (auto u : utils::used_vars(inst)) {
                    if (!kill.contains(*u)) gen.insert(*u);
                }
                if (auto def = utils::defined_var(inst); def) {
                    kill.insert(*def);
                }
            }
            if (auto exit_use = utils::used_var(block->exit()); exit_use) {
                if (!kill.contains(*exit_use)) gen.insert(*exit_use);
            }
            ctx.gen[block] = gen;
            ctx.kill[block] = kill;
        }
        ctx.global_variables = Data::empty();
        for (auto& global_alloc : prog.getGlobals()) {
            ctx.global_variables.insert(LeftValue{global_alloc->value()});
        }
        return ctx;
    }
};

static_assert(has_context<Liveness>::value);
static_assert(is_flow_trait_v<Liveness>);

}  // namespace ir::analysis::flow
