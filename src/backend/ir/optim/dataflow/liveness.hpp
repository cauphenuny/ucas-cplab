/// @brief live variable analysis
#pragma once

#include "backend/ir/ir.hpp"
#include "framework.hpp"
#include "utils/match.hpp"

namespace ir::optim::flow {

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
    };

    static Data boundary(Context& ctx) {
        return ctx.global_variables;
    }
    static constexpr auto top = Data::empty;
    static constexpr auto meet = Data::union_set;

    static Data transfer(Block& blk, const Data& out, Context& ctx) {
        return Data::union_set(out.difference(ctx.kill[&blk]), ctx.gen[&blk]);
    }

    static Context init(const ControlFlowGraph& cfg, const Program& prog) {
        Context ctx;
        // TODO
        auto convert = [](const Value& val) -> std::optional<ElemType> {
            return match(
                val, [](const LeftValue& v) -> std::optional<ElemType> { return v; },
                [](const auto& v) -> std::optional<ElemType> { return std::nullopt; });
        };
        auto use = [&](const Inst& inst) {
            std::unordered_set<ElemType> res;
            match(
                inst,
                [&](const UnaryInst& i) {
                    if (auto use = convert(i.operand); use) res.insert(*use);
                    if (i.op == UnaryInstOp::STORE) {  // store uses 'result' as target
                        if (auto use = convert(i.result); use) res.insert(*use);
                    }
                },
                [&](const BinaryInst& i) {
                    if (auto use = convert(i.lhs); use) res.insert(*use);
                    if (auto use = convert(i.rhs); use) res.insert(*use);
                    if (i.op == InstOp::STORE) {  // store uses 'result' as base address
                        if (auto use = convert(i.result); use) res.insert(*use);
                    }
                },
                [&](const CallInst& i) {
                    for (const auto& arg : i.args) {
                        if (auto use = convert(arg); use) res.insert(*use);
                    }
                },
                [&](const PhiInst& p) {
                    for (const auto& [block, val] : p.args) {
                        if (auto use = convert(val); use) res.insert(*use);
                    }
                });
            return res;
        };
        auto defs = [&](const Inst& inst) -> std::optional<ElemType> {
            using T = std::optional<ElemType>;
            return match(
                inst,
                [&](const UnaryInst& i) -> T {
                    return (i.op == UnaryInstOp::STORE) ? std::nullopt : convert(i.result);
                },
                [&](const BinaryInst& i) -> T {
                    return (i.op == InstOp::STORE) ? std::nullopt : convert(i.result);
                },
                [&](const CallInst& i) -> T { return convert(i.result); },
                [&](const PhiInst& p) -> T { return convert(p.result); });
        };
        for (auto& block_box : cfg.func.blocks()) {
            auto gen = Data::empty(), kill = Data::empty();
            auto block = block_box.get();
            for (auto& inst : block->insts()) {
                auto inst_uses = use(inst);
                for (const auto& u : inst_uses) {
                    if (!kill.contains(u)) gen.insert(u);
                }
                if (auto def = defs(inst); def) {
                    kill.insert(*def);
                }
            }
            match(
                block->exit(),
                [&](const BranchExit& exit) {
                    auto use = convert(exit.cond);
                    if (use && !kill.contains(*use)) gen.insert(*use);
                },
                [&](const JumpExit&) {},
                [&](const ReturnExit& exit) {
                    auto use = convert(exit.exp);
                    if (use && !kill.contains(*use)) gen.insert(*use);
                });
            ctx.gen[block] = gen;
            ctx.kill[block] = kill;
        }
        ctx.global_variables = Data::empty();
        for (auto& global_alloc : prog.getGlobals()) {
            auto use = convert(LeftValue{global_alloc->value()});
            ctx.global_variables.insert(*use);
        }
        return ctx;
    }
};

static_assert(has_context<Liveness>::value);
static_assert(is_flow_trait_v<Liveness>);

}  // namespace ir::optim::flow
