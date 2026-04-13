#pragma once

#include "framework.hpp"
#include "utils/match.hpp"

namespace ir::optim::flow {

struct ActiveVariables {
    static constexpr bool is_forward = false;
    using ElemType = std::variant<size_t, NameDef>;
    static std::string print(const ElemType& elem) {
        return match(
            elem, [](size_t id) { return fmt::format("${}", id); },
            [](const NameDef& def) { return match(def, [](const auto& d) { return d->name; }); });
    }

    using Data = Set<ElemType, print>;
    struct Context {
        std::unordered_map<const Block*, Data> gen, kill;
    };

    static constexpr auto boundary = Data::empty;
    static constexpr auto top = Data::empty;
    static constexpr auto meet = Data::union_set;

    static Data transfer(const Block& blk, const Data& out, Context& ctx) {
        return Data::union_set(out.difference(ctx.kill[&blk]), ctx.gen[&blk]);
    }

    static Context init(const ControlFlowGraph& cfg) {
        Context ctx;
        // TODO
        auto convert = [](const Value& val) -> std::optional<ElemType> {
            return match(
                val,
                [](const LeftValue& v) -> std::optional<ElemType> {
                    return match(
                        v, [](const NamedValue& nv) -> ElemType { return nv.def; },
                        [](const TempValue& tv) -> ElemType { return tv.id; });
                },
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
                [&](const CallInst& i) -> T { return convert(i.result); });
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
                    if (!use) return;
                    if (!kill.contains(*use)) gen.insert(*use);
                },
                [&](const JumpExit&) {},
                [&](const ReturnExit& exit) {
                    auto use = convert(exit.exp);
                    if (!use) return;
                    if (!kill.contains(*use)) gen.insert(*use);
                });
            ctx.gen[block] = gen;
            ctx.kill[block] = kill;
        }
        return ctx;
    }
};

static_assert(has_context<ActiveVariables>::value);
static_assert(is_flow_trait_v<ActiveVariables>);

}  // namespace ir::optim::flow
