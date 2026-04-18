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

auto define_var(Inst& inst) -> LeftValue* {
    auto is_def = [&](LeftValue& res) {
        return match(res, [&](NamedValue& n) { return true; }, [](auto&) { return false; });
    };
    return match(
        inst, [&](PhiInst& p) { return is_def(p.result) ? &p.result : nullptr; },
        [&](BinaryInst& b) {
            return (b.op != InstOp::STORE && is_def(b.result)) ? &b.result : nullptr;
        },
        [&](UnaryInst& u) {
            return (u.op != UnaryInstOp::STORE && is_def(u.result)) ? &u.result : nullptr;
        },
        [&](CallInst& c) { return is_def(c.result) ? &c.result : nullptr; });
}

auto use_vars(Inst& inst) -> std::vector<LeftValue*> {
    std::vector<LeftValue*> uses;
    auto is_named = [&](const LeftValue& v) {
        return match(v, [&](const NamedValue& n) { return true; }, [](auto&) { return false; });
    };
    auto unwrap = [&](Value& v) -> LeftValue* {
        return match(
            v, [&](LeftValue& n) { return is_named(n) ? &n : nullptr; },
            [](auto&) -> LeftValue* { return nullptr; });
    };
    match(
        inst,
        [&](PhiInst& p) {
            for (auto& [_, arg] : p.args) {
                if (auto lval = unwrap(arg); lval) uses.push_back(lval);
            }
        },
        [&](BinaryInst& b) {
            if (auto lval = unwrap(b.lhs); lval) uses.push_back(lval);
            if (auto lval = unwrap(b.rhs); lval) uses.push_back(lval);
            if (b.op == InstOp::STORE && is_named(b.result)) uses.emplace_back(&b.result);
        },
        [&](UnaryInst& u) {
            if (auto lval = unwrap(u.operand); lval) uses.push_back(lval);
            if (u.op == UnaryInstOp::STORE && is_named(u.result)) uses.emplace_back(&u.result);
        },
        [&](CallInst& c) {
            for (auto& arg : c.args) {
                if (auto lval = unwrap(arg); lval) uses.push_back(lval);
            }
        });
    return uses;
}

struct AddPhi : Pass {
    void apply(Program& prog) override {
        for (auto& func : prog.getFuncs()) {
            transform(*func, prog);
        }
    }

private:
    auto definitions(const Func& func, const Alloc* alloc) {
        std::unordered_set<Block*> defs;
        auto val = LeftValue{alloc->value()};

        for (const auto& block_box : func.blocks()) {
            auto block = block_box.get();
            for (auto& inst : block->insts()) {
                if (auto def = define_var(inst); def && *def == val) {
                    defs.insert(block);
                }
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
            if (alloc->immutable) continue;  // immutable value does not need phi
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
                            phi.args[pred] = LeftValue{val};
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
    void apply(Program& prog) override {
        for (auto& func : prog.getFuncs()) {
            transform(*func, prog);
        }
    }

private:
    std::unordered_set<const Alloc*> need_rename;
    std::unordered_map<const Alloc*, size_t> version;
    std::unordered_map<const Alloc*, std::stack<SSAValue>> rename_stack;
    std::unique_ptr<ControlFlowGraph> cfg;
    std::unique_ptr<DataFlow<flow::Dominance>> dom_flow;
    std::unique_ptr<DominanceTree> dom_tree;

    void rename(Block& block, const Func& func) {
        // fmt::println(stderr, "Renaming block {}", block.label);
        auto alloc_of = [](const LeftValue& v) -> const Alloc* {
            return match(
                v,
                [&](const NamedValue& n) -> const Alloc* {
                    return match(
                        n.def, [&](const Alloc* a) { return a; },
                        [](auto) -> const Alloc* { return nullptr; });
                },
                [](const auto&) -> const Alloc* { return nullptr; });
        };
        std::unordered_map<const Alloc*, size_t> push_count;
        for (auto& inst : block.insts()) {
            // 1. Rename uses
            if (std::get_if<PhiInst>(&inst) == nullptr) {
                auto uses = use_vars(inst);
                for (auto& use : uses) {
                    auto alloc = alloc_of(*use);
                    if (alloc && need_rename.count(alloc)) {
                        if (rename_stack[alloc].size()) {
                            *use = rename_stack[alloc].top();
                        }
                    }
                }
            }

            // 2. Rename definition
            auto def = define_var(inst);
            if (def) {
                auto alloc = alloc_of(*def);
                if (alloc && need_rename.count(alloc)) {
                    auto ssa_value = SSAValue(type_of(*def), alloc, version[alloc]++);
                    *def = ssa_value;
                    // fmt::println(stderr, "push {}", ssa_value);
                    rename_stack[alloc].push(ssa_value);
                    push_count[alloc]++;
                }
            }
        }
        // 3. Rename uses in Exit
        if (block.hasExit()) {
            auto unwrap = [&](Value& v) -> LeftValue* {
                return match(
                    v,
                    [&](LeftValue& n) -> LeftValue* {
                        return match(
                            n, [&](NamedValue& nv) { return &n; },
                            [](auto&) -> LeftValue* { return nullptr; });
                    },
                    [](auto&) -> LeftValue* { return nullptr; });
            };
            auto update_val = [&](Value& v) {
                if (auto lval = unwrap(v); lval) {
                    auto alloc = alloc_of(*lval);
                    if (alloc && need_rename.count(alloc)) {
                        if (rename_stack[alloc].size()) {
                            *lval = rename_stack[alloc].top();
                        }
                    }
                }
            };
            match(
                block.exit(), [&](ReturnExit& e) { update_val(e.exp); },
                [&](BranchExit& e) { update_val(e.cond); }, [&](auto&) {});
        }
        for (auto& succ : cfg->succ[&block]) {
            for (auto& inst : succ->insts()) {
                if (auto phi = std::get_if<PhiInst>(&inst)) {
                    for (auto& [pred, arg] : phi->args) {
                        if (pred == &block) {
                            if (auto lval = std::get_if<LeftValue>(&arg); lval) {
                                auto alloc = alloc_of(*lval);
                                if (need_rename.count(alloc)) {
                                    if (rename_stack[alloc].size()) {
                                        arg = LeftValue{rename_stack[alloc].top()};
                                    }
                                }
                            }
                        }
                    }
                } else {
                    break;  // phi nodes are always at the beginning of the block
                }
            }
        }
        for (auto& child : dom_tree->children(&block)) {
            rename(*child, func);
        }
        for (auto& [alloc, count] : push_count) {
            for (size_t i = 0; i < count; i++) {
                rename_stack[alloc].pop();
            }
        }
    }

    void transform(Func& func, Program& program) {
        need_rename.clear();
        version.clear();
        rename_stack.clear();
        cfg = std::make_unique<ControlFlowGraph>(func);
        dom_flow = std::make_unique<DataFlow<flow::Dominance>>(*cfg, program);
        dom_tree = std::make_unique<DominanceTree>(*dom_flow);

        for (const auto& alloc : func.locals()) {
            if (!alloc->immutable) {
                need_rename.insert(alloc.get());
            }
        }
        if (need_rename.size()) {
            rename(*func.entrance(), func);
        }
        for (const auto& alloc : func.locals()) {
            alloc->immutable = true;  // after renaming, all allocs can be treated as immutable
        }
    }
};

}  // namespace ssa

using ToSSA = Compose<ssa::AddPhi, ssa::Rename>;

struct SSAValue2TempValue : Pass {
    void apply(Program& prog) override {
        std::unordered_map<SSAValue, TempValue> ssa_to_temp;
        for (auto& func : prog.getFuncs()) {
            std::unordered_map<const Alloc*, bool> used;
            for (auto& block : func->blocks()) {
                auto convert = [&](LeftValue* v) {
                    if (auto ssa = std::get_if<SSAValue>(v); ssa) {
                        if (!ssa_to_temp.count(*ssa)) {
                            ssa_to_temp[*ssa] = func->newTemp(ssa->type, block.get());
                        }
                        *v = ssa_to_temp[*ssa];
                    } else if (auto named = std::get_if<NamedValue>(v); named) {
                        if (auto alloc = std::get_if<const Alloc*>(&named->def); alloc) {
                            used[*alloc] = true;
                        }
                    }
                };
                for (auto& inst : block->insts()) {
                    for (auto& val : values(inst)) {
                        convert(val);
                    }
                }
                for (auto& val : values(block->exit())) {
                    convert(val);
                }
            }
            std::vector<std::unique_ptr<Alloc>> pruned_locals;
            for (auto& alloc : func->locals()) {
                if (used.count(alloc.get())) {
                    pruned_locals.push_back(std::move(alloc));
                }
            }
            func->locals() = std::move(pruned_locals);
        }
    }

private:
    auto values(Inst& inst) -> std::vector<LeftValue*> {
        std::vector<LeftValue*> vals;
        Match{inst}(
            [&](UnaryInst& u) {
                if (auto lval = std::get_if<LeftValue>(&u.operand); lval) vals.push_back(lval);
                vals.push_back(&u.result);
            },
            [&](BinaryInst& b) {
                if (auto lval = std::get_if<LeftValue>(&b.lhs); lval) vals.push_back(lval);
                if (auto lval = std::get_if<LeftValue>(&b.rhs); lval) vals.push_back(lval);
                vals.push_back(&b.result);
            },
            [&](PhiInst& p) {
                for (auto& [_, arg] : p.args) {
                    if (auto lval = std::get_if<LeftValue>(&arg); lval) vals.push_back(lval);
                }
                vals.push_back(&p.result);
            },
            [&](CallInst& c) {
                for (auto& arg : c.args) {
                    if (auto lval = std::get_if<LeftValue>(&arg); lval) vals.push_back(lval);
                }
                vals.push_back(&c.result);
            });
        return vals;
    }
    auto values(Exit& exit) -> std::vector<LeftValue*> {
        std::vector<LeftValue*> vals;
        Match{exit}([](JumpExit&) {},
                    [&](BranchExit& e) {
                        if (auto lval = std::get_if<LeftValue>(&e.cond); lval) vals.push_back(lval);
                    },
                    [&](ReturnExit& e) {
                        if (auto lval = std::get_if<LeftValue>(&e.exp); lval) vals.push_back(lval);
                    });
        return vals;
    }
};

}  // namespace ir::optim::pass