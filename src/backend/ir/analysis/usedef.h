#pragma once

#include "backend/ir/ir.h"

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace ir::analysis {

struct UseDefInfo : Callback {
    UseDefInfo(Program& program);
    ~UseDefInfo();
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
    struct DefSite {
        Inst* inst;  // is defined by inst->result
    };

    void after_add(Inst* it) override;
    void after_add(Exit* exit) override;
    void before_erase(Inst* it) override;
    void before_erase(Exit* exit) override;

    [[nodiscard]] auto uses_of(const LeftValue& val) const
        -> std::unordered_set<UseSite, UseSiteHash>;
    [[nodiscard]] auto def_of(const LeftValue& val) const -> std::optional<DefSite>;

    void replace_all_uses_with(const LeftValue& old_val, const Value& new_val);
    void replace_all_links_with(Block* old_block, Block* as_source, Block* as_target);

    void verify() const;

private:
    void add_use(Value* val, std::variant<Inst*, Exit*> it);
    void erase_use(Value* val, std::variant<Inst*, Exit*> it);
    Program& program;
    std::unordered_map<LeftValue, DefSite> def_sites;
    std::unordered_map<LeftValue, std::unordered_set<UseSite, UseSiteHash>> use_sites;
    std::unordered_map<Block*, std::unordered_set<std::reference_wrapper<Block*>>> source_links, target_links;
};

}  // namespace ir::analysis