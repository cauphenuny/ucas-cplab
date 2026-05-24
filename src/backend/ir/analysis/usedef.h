#pragma once

#include "backend/ir/ir.h"

#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace ir::analysis {

using DefSite = Inst*;

struct UseSite {
    std::variant<Inst*, Exit*> site;
    Value* operand;
    friend bool operator==(const UseSite& a, const UseSite& b) {
        return a.site == b.site && a.operand == b.operand;
    }
};
struct UseSiteHash {
    std::size_t operator()(const UseSite& site) const {
        return std::hash<std::variant<Inst*, Exit*>>{}(site.site) ^
               std::hash<Value*>{}(site.operand);
    }
};

struct UseInfo : Callback {
    UseInfo(Program& program);
    ~UseInfo();
    void after_add(Inst* it) override;
    void after_add(Exit* exit) override;
    void before_erase(Inst* it) override;
    void before_erase(Exit* exit) override;
    [[nodiscard]] auto uses_of(const LeftValue& val) const
        -> std::unordered_set<UseSite, UseSiteHash>;

    bool replace_all_uses_with(const LeftValue& old_val, const Value& new_val);

    void verify() const;

private:
    void add_use(Value* val, std::variant<Inst*, Exit*> it);
    void erase_use(Value* val, std::variant<Inst*, Exit*> it);
    std::unordered_map<LeftValue, std::unordered_set<UseSite, UseSiteHash>> use_sites;
    Program& program;
};

struct DefInfo : Callback {
    DefInfo(Program& program);
    ~DefInfo();
    void after_add(Inst* it) override;
    void after_add(Exit* exit) override {}
    void before_erase(Inst* it) override;
    void before_erase(Exit* exit) override {}
    [[nodiscard]] auto def_of(const LeftValue& val) const -> std::optional<DefSite>;

    void verify() const;
    [[nodiscard]] auto all_defs() const -> const std::unordered_map<LeftValue, DefSite>&;
    bool replace_all_defs_with(const LeftValue& old_var, const LeftValue& new_var);

private:
    std::unordered_map<LeftValue, DefSite> def_sites;
    Program& program;
};

struct MultiDefInfo : Callback {
    MultiDefInfo(Program& program);
    ~MultiDefInfo();
    void after_add(Inst* it) override;
    void after_add(Exit* exit) override {}
    void before_erase(Inst* it) override;
    void before_erase(Exit* exit) override {}

    [[nodiscard]] auto def_of(const LeftValue& val) const -> std::unordered_set<DefSite>;
    [[nodiscard]] auto all_defs() const
        -> const std::unordered_map<LeftValue, std::unordered_set<DefSite>>&;
    bool replace_all_defs_with(const LeftValue& old_var, const LeftValue& new_var);

    void verify() const;

private:
    std::unordered_map<LeftValue, std::unordered_set<DefSite>> def_sites;
    Program& program;
};

template <typename Use, typename Def> struct UniversalUseDefGraph {
    UniversalUseDefGraph(Program& program) : uses(program), defs(program) {}

    [[nodiscard]] auto uses_of(const LeftValue& val) const {
        return uses.uses_of(val);
    }
    [[nodiscard]] auto def_of(const LeftValue& val) const {
        return defs.def_of(val);
    }

    bool replace_all_uses_with(const LeftValue& old_val, const Value& new_val) {
        return uses.replace_all_uses_with(old_val, new_val);
    }

    bool replace_all_defs_with(const LeftValue& old_var, const LeftValue& new_var) {
        return defs.replace_all_defs_with(old_var, new_var);
    }

    void verify() const {
        uses.verify(), defs.verify();
    }

    auto all_defs() const {
        return defs.all_defs();
    }

    Use uses;
    Def defs;
};

using UseDefGraph = UniversalUseDefGraph<UseInfo, DefInfo>;
using NonSSAUseDefGraph = UniversalUseDefGraph<UseInfo, MultiDefInfo>;

}  // namespace ir::analysis
