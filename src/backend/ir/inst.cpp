#include "backend/ir/ir.h"

#include <cstddef>
#include <cstring>
#include <string>

namespace ir {

namespace {}  // namespace

auto CallInst::toString() const -> std::string {
    std::string arg_str;
    for (auto&& arg : args) {
        arg_str += fmt::format("{}, ", arg);
    }
    if (!arg_str.empty()) arg_str.pop_back(), arg_str.pop_back();
    return fmt::format("{}: {} = {}({});", result, type_of(result), func, arg_str);
}

auto PhiInst::toString() const -> std::string {
    std::string arg_str;
    for (auto&& [block, val] : args) {
        arg_str += fmt::format("'{}: {}, ", block->label, val);
    }
    if (!arg_str.empty()) arg_str.pop_back(), arg_str.pop_back();
    return fmt::format("{}: {} = phi({});", result, type_of(result), arg_str);
}
auto PhiInst::operator[](Block* block) const -> const Value& {
    for (auto& [b, v] : args) {
        if (b == block) {
            return v;
        }
    }
    throw COMPILER_ERROR(fmt::format("Block {} not found in PhiInst", block->label));
}
bool PhiInst::contains(Block* block) const {
    for (auto& [b, v] : args) {
        if (b == block) return true;
    }
    return false;
}

auto BranchExit::toString() const -> std::string {
    return fmt::format("=> if {} {{ '{} }} else {{ '{} }};", cond,
                       true_target ? true_target->label : "<unknown>",
                       false_target ? false_target->label : "<unknown>");
}

JumpExit::JumpExit(Block* target) : target(target) {
    if (!target) {
        throw COMPILER_ERROR("target block cannot be null");
    }
}

auto JumpExit::toString() const -> std::string {
    return fmt::format("=> '{};", target ? target->label : "<unknown>");
}

}  // namespace ir
