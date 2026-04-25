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
        Block* block;
        std::variant<Inst*, Exit*> site;
        Value* operand;
        friend bool operator==(const UseSite& a, const UseSite& b) {
            return a.block == b.block && a.site == b.site && a.operand == b.operand;
        }
    };
    struct UseSiteHash {
        std::size_t operator()(const UseSite& site) const {
            return std::hash<Block*>{}(site.block) ^
                   std::hash<std::variant<Inst*, Exit*>>{}(site.site) ^
                   std::hash<Value*>{}(site.operand);
        }
    };
    struct DefSite {
        Block* block;
        Inst* inst;  // is defined by inst->result
    };

    void after_inst_add(Block* block, Inst* it) override;
    void before_inst_erase(Block* block, Inst* it) override;
    void after_exit_add(Block* block) override;
    void before_exit_erase(Block* block) override;
    void after_block_add(Func* func, Block* block) override;
    void before_block_erase(Func* func, Block* block) override;

    [[nodiscard]] auto uses_of(const LeftValue& val) const
        -> std::unordered_set<UseSite, UseSiteHash>;
    [[nodiscard]] auto def_of(const LeftValue& val) const -> std::optional<DefSite>;

    void replace_all_uses_with(const LeftValue& old_val, const Value& new_val);

    void verify() const;

private:
    void add_use(Value* val, Block* block, std::variant<Inst*, Exit*> it);
    void erase_use(Value* val, Block* block, std::variant<Inst*, Exit*> it);
    Program& program;
    std::unordered_map<LeftValue, DefSite> def_sites;
    std::unordered_map<LeftValue, std::unordered_set<UseSite, UseSiteHash>> use_sites;
};

}  // namespace ir::analysis