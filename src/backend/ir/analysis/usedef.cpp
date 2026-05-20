#include "usedef.h"

#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"

#include <optional>
#include <unordered_set>
#include <utility>
#include <variant>

namespace ir::analysis {

UseInfo::UseInfo(Program& program) : program(program) {
    program.addCallback(this);
}
UseInfo::~UseInfo() {
    program.removeCallback(this);
}

void UseInfo::add_use(Value* val, std::variant<Inst*, Exit*> it) {
    if (auto lval = std::get_if<LeftValue>(val)) {
        use_sites[*lval].insert(UseSite{it, val});
    }
}

void UseInfo::erase_use(Value* val, std::variant<Inst*, Exit*> it) {
    if (auto lval = std::get_if<LeftValue>(val)) {
        auto site_it = use_sites[*lval].find(UseSite{it, val});
        if (site_it != use_sites[*lval].end()) {
            use_sites[*lval].erase(site_it);
        }
    }
}

void UseInfo::after_add(Inst* it) {
    for (auto use : utils::used(*it)) {
        add_use(use, it);
    }
}

void UseInfo::before_erase(Inst* it) {
    for (auto& use : utils::used(*it)) {
        erase_use(use, it);
    }
}

void UseInfo::after_add(Exit* exit) {
    if (auto use = utils::used(*exit)) {
        add_use(use, exit);
    }
}

void UseInfo::before_erase(Exit* exit) {
    if (auto use = utils::used(*exit)) {
        erase_use(use, exit);
    }
}

auto UseInfo::uses_of(const LeftValue& val) const -> std::unordered_set<UseSite, UseSiteHash> {
    if (use_sites.count(val)) {
        return use_sites.at(val);
    }
    return {};
}

bool UseInfo::replace_all_uses_with(const LeftValue& old_val, const Value& new_val) {
    if (!use_sites.count(old_val)) return false;
    auto sites = use_sites[old_val];
    if (sites.empty()) return false;
    for (const auto& site : sites) {
        Match{site.site}([&](auto container) {
            erase_use(site.operand, container);
            *site.operand = new_val;
            add_use(site.operand, container);
        });
    }
    return true;
}

void UseInfo::verify() const {
    auto reference = UseInfo(program);
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

DefInfo::DefInfo(Program& program) : program(program) {
    program.addCallback(this);
}

DefInfo::~DefInfo() {
    program.removeCallback(this);
}

void DefInfo::after_add(Inst* it) {
    def_sites[*utils::defined_var(*it)] = it;
}

void DefInfo::before_erase(Inst* it) {
    def_sites.erase(*utils::defined_var(*it));
}

auto DefInfo::def_of(const LeftValue& val) const -> std::optional<DefSite> {
    if (def_sites.count(val)) {
        return def_sites.at(val);
    }
    return std::nullopt;
}

void DefInfo::verify() const {
    auto reference = DefInfo(program);
    for (const auto& [val, def] : def_sites) {
        auto ref_def = reference.def_of(val);
        if (!ref_def) {
            throw COMPILER_ERROR(fmt::format("Def site of {} not found in reference", val));
        }
        if (def != *ref_def) {
            throw COMPILER_ERROR(fmt::format("Def site of {} does not match reference", val));
        }
    }
    for (const auto& [val, def] : reference.def_sites) {
        auto this_def = def_of(val);
        if (!this_def) {
            throw COMPILER_ERROR(fmt::format("Def site of {} not found in this", val));
        }
    }
}

MultiDefInfo::MultiDefInfo(Program& program) : program(program) {
    program.addCallback(this);
}
MultiDefInfo::~MultiDefInfo() {
    program.removeCallback(this);
}

void MultiDefInfo::after_add(Inst* it) {
    auto def = utils::defined_var(*it);
    if (def) {
        def_sites[*def].insert(it);
    }
}

void MultiDefInfo::before_erase(Inst* it) {
    auto def = utils::defined_var(*it);
    if (def) {
        auto site_it = def_sites[*def].find(it);
        if (site_it != def_sites[*def].end()) {
            def_sites[*def].erase(site_it);
        }
    }
}

auto MultiDefInfo::def_of(const LeftValue& val) const -> std::unordered_set<DefSite> {
    if (def_sites.count(val)) {
        return def_sites.at(val);
    }
    return {};
}

void MultiDefInfo::verify() const {
    auto reference = MultiDefInfo(program);
    for (const auto& [val, defs] : def_sites) {
        auto ref_defs = reference.def_of(val);
        if (defs.size() != ref_defs.size()) {
            throw COMPILER_ERROR(fmt::format("Def sites of {} size does not match reference", val));
        }
        for (const auto& def : defs) {
            if (!ref_defs.count(def)) {
                throw COMPILER_ERROR(fmt::format("Def site of {} not found in reference", val));
            }
        }
    }
    for (const auto& [val, defs] : reference.def_sites) {
        auto this_defs = def_of(val);
        for (const auto& def : defs) {
            if (!this_defs.count(def)) {
                throw COMPILER_ERROR(fmt::format("Def site of {} not found in this", val));
            }
        }
    }
}

}  // namespace ir::analysis
