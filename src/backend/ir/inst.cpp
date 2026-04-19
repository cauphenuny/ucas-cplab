#include "backend/ir/ir.h"

#include <cstddef>
#include <cstring>
#include <string>

namespace ir {

namespace {
}  // namespace

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
    return fmt::format("{}: {} = $phi({});", result, type_of(result), arg_str);
}

auto BranchExit::toString() const -> std::string {
    return fmt::format("branch {} ? '{} : '{};", cond,
                       true_target ? true_target->label : "<unknown>",
                       false_target ? false_target->label : "<unknown>");
}

JumpExit::JumpExit(Block* target) : target(target) {
    if (!target) {
        throw COMPILER_ERROR("target block cannot be null");
    }
}

auto JumpExit::toString() const -> std::string {
    return fmt::format("jump '{};", target ? target->label : "<unknown>");
}

}  // namespace ir
