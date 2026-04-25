#include "usedef.h"

#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"

#include <optional>
#include <unordered_set>
#include <variant>

namespace ir::analysis {

UseDefInfo::UseDefInfo(Program& program) : program(program) {
    for (const auto& func : program.funcs()) {
        for (const auto& block : func->blocks()) {
            after_block_add(func.get(), block.get());
        }
    }
    program.addCallback(this);
}

UseDefInfo::~UseDefInfo() {
    program.removeCallback(this);
}

void UseDefInfo::add_use(Value* val, Block* block, std::variant<Inst*, Exit*> it) {
    if (auto lval = std::get_if<LeftValue>(val)) {
        use_sites[*lval].insert(UseSite{block, it, val});
    }
}

void UseDefInfo::erase_use(Value* val, Block* block, std::variant<Inst*, Exit*> it) {
    if (auto lval = std::get_if<LeftValue>(val)) {
        auto site_it = use_sites[*lval].find(UseSite{block, it, val});
        if (site_it != use_sites[*lval].end()) {
            use_sites[*lval].erase(site_it);
        }
    }
}

void UseDefInfo::after_inst_add(Block* block, Inst* it) {
    def_sites[*utils::defined_var(*it)] = {block, it};
    for (auto use : utils::used(*it)) {
        add_use(use, block, it);
    }
}

void UseDefInfo::before_inst_erase(Block* block, Inst* it) {
    def_sites.erase(*utils::defined_var(*it));
    for (auto& use : utils::used(*it)) {
        erase_use(use, block, it);
    }
}

void UseDefInfo::after_exit_add(Block* block) {
    if (auto use = utils::used(block->exit())) {
        add_use(use, block, &block->exit());
    }
}

void UseDefInfo::before_exit_erase(Block* block) {
    if (auto use = utils::used(block->exit())) {
        erase_use(use, block, &block->exit());
    }
}

void UseDefInfo::after_block_add(Func* func, Block* block) {
    for (auto& inst : block->insts()) {
        after_inst_add(block, &inst);
    }
    after_exit_add(block);
}

void UseDefInfo::before_block_erase(Func* func, Block* block) {
    for (auto& inst : block->insts()) {
        before_inst_erase(block, &inst);
    }
    before_exit_erase(block);
}

auto UseDefInfo::uses_of(const LeftValue& val) const -> std::unordered_set<UseSite, UseSiteHash> {
    if (use_sites.count(val)) {
        return use_sites.at(val);
    }
    return {};
}

auto UseDefInfo::def_of(const LeftValue& val) const -> std::optional<DefSite> {
    if (def_sites.count(val)) {
        return def_sites.at(val);
    }
    return std::nullopt;
}

void UseDefInfo::replace_all_uses_with(const LeftValue& old_val, const Value& new_val) {
    if (!use_sites.count(old_val)) return;
    auto sites = use_sites[old_val];
    for (const auto& site : sites) {
        Match{site.site}([&](auto handler) {
            erase_use(site.operand, site.block, handler);
            *site.operand = new_val;
            add_use(site.operand, site.block, handler);
        });
    }
}

void UseDefInfo::verify() const {
    auto reference = UseDefInfo(program);
    for (const auto& [val, def] : def_sites) {
        auto ref_def = reference.def_of(val);
        if (!ref_def) {
            throw COMPILER_ERROR(fmt::format("Def site of {} not found in reference", val));
        }
        if (def.block != ref_def->block || def.inst != ref_def->inst) {
            throw COMPILER_ERROR(fmt::format("Def site of {} does not match reference", val));
        }
    }
    for (const auto& [val, def] : reference.def_sites) {
        auto this_def = def_of(val);
        if (!this_def) {
            throw COMPILER_ERROR(fmt::format("Def site of {} not found in this", val));
        }
    }
    for (const auto& [val, uses] : use_sites) {
        auto ref_uses = reference.uses_of(val);
        if (uses.size() != ref_uses.size()) {
            throw COMPILER_ERROR(fmt::format("Use sites of {} size does not match reference", val));
        }
        for (const auto& use : uses) {
            if (!ref_uses.count(use)) {
                throw COMPILER_ERROR(fmt::format("Use site of {} not found in reference", val));
            }
        }
    }
    for (const auto& [val, uses] : reference.use_sites) {
        auto this_uses = uses_of(val);
        for (const auto& use : uses) {
            if (!this_uses.count(use)) {
                throw COMPILER_ERROR(fmt::format("Use site of {} not found in this", val));
            }
        }
    }
}

}  // namespace ir::analysis