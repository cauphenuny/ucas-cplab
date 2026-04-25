/// @brief Inline Pass, requires SSA

#pragma once

/// pipeline:
/// for each call site:
/// 1. split block
/// 2. clone func
/// 3. rename temps, merge to caller's temps
/// 4. add prologue block consisting mov inst for copying parameters
/// 5. collect exit blocks, add epilogue block consisting a phi inst for merging return values
/// 6. move params and locals to caller's locals
/// 7. move func's block to caller
/// 8. connect blocks

#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/op.hpp"
#include "framework.hpp"

#include <list>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ir::optim {

struct Inlining : SSAPass {
    Inlining(size_t threshold = 8) : threshold(threshold) {}

    bool apply(Program& prog, SSAPassContext& ctx) override {
        if (!prog.is_ssa) {
            throw COMPILER_ERROR("Inlining requires SSA form");
        }
        for (auto it = prog.funcs().begin(); it != prog.funcs().end(); ++it) {
            if (inlinable(**it)) {
                auto* target = it->get();
                size_t id = 0;
                while (auto site = find_site(target, prog)) {
                    inline_site(*site, target, id++, prog);
                }
                prog.removeFunc(it);
                return true;
            }
        }
        return false;
    }

private:
    struct CallSite {
        std::list<Inst>::iterator inst_it;
        Block* block;
        Func* func;
    };

    void inline_site(const CallSite& site, Func* inline_func, size_t id, Program& prog) {
        auto& [it, block, caller] = site;

        auto prefix = fmt::format("inline_{}_{}_", inline_func->name, id);
        auto prologue = std::make_unique<Block>(prefix + "prologue");
        auto epilogue = std::make_unique<Block>(prefix + "epilogue");

        auto callee = inline_func->clone(prefix);

        std::unordered_map<size_t, TempValue> temp_map;
        for (size_t id = 0; id < callee->temps().size(); id++) {
            auto [type, block] = callee->temps()[id];
            temp_map[id] = caller->newTemp(type, block);
        }
        for (auto var : analysis::utils::vars(*callee)) {
            if (auto temp = std::get_if<TempValue>(var); temp) {
                *var = temp_map[temp->id];
            }
        }

        auto remain = caller->split(block, it, JumpExit{prologue.get()}, prefix + "return");
        auto call = std::get<CallInst>(remain->pop_front());

        for (size_t i = 0; i < call.args.size(); i++) {
            prologue->add(UnaryInst{.op = UnaryInstOp::MOV,
                                    .result = callee->params[i]->value(),
                                    .operand = call.args[i]});
        }
        prologue->setExit(JumpExit{callee->entrance()});

        auto return_values = std::vector<std::pair<Block*, Value>>();
        for (auto exit_block : callee->exits()) {
            auto exit = std::get<ReturnExit>(exit_block->exit());
            return_values.emplace_back(exit_block, exit.exp);
            exit_block->exit() = JumpExit{epilogue.get()};
        }

        epilogue->add(PhiInst{.result = call.result, .args = return_values});
        epilogue->setExit(JumpExit{remain});

        for (auto& param : callee->params) {
            caller->addLocal(std::move(param));
        }
        for (auto& local : callee->locals()) {
            caller->addLocal(std::move(local));
        }
        caller->addBlock(std::move(prologue));
        for (auto& block : callee->blocks()) {
            caller->addBlock(std::move(block));
        }
        caller->addBlock(std::move(epilogue));
    }

    std::optional<CallSite> find_site(Func* inline_func, Program& prog) {
        for (auto& func : prog.funcs()) {
            for (auto& block : func->blocks()) {
                for (auto it = block->insts().begin(); it != block->insts().end(); ++it) {
                    if (auto call = std::get_if<CallInst>(&*it)) {
                        if (call->func.def == NameDef{inline_func}) {
                            return CallSite{it, block.get(), func.get()};
                        }
                    }
                }
            }
        }
        return std::nullopt;
    }

    bool inlinable(Func& func) const {
        return func.name != "main" && func.numInsts() <= threshold && !func.hasRecursiveCall();
    }

    size_t threshold;
};

}  // namespace ir::optim