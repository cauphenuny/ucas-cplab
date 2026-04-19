#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "type.hpp"

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace ir {

Func::Func(Type ret_type, std::string name, std::vector<std::unique_ptr<Alloc>> params)
    : ret_type(std::move(ret_type)), name(std::move(name)), params(std::move(params)) {}

auto Func::toString() const -> std::string {
    std::string params_str = "";
    for (const auto& param : this->params) {
        params_str += fmt::format("{}: {}, ", param->name, param->type);
    }
    if (!params_str.empty()) params_str.pop_back(), params_str.pop_back();

    std::string str;
    if (ret_type == ir::type::construct<void>()) {
        str += fmt::format("fn {}({}) {{\n", name, params_str);
    } else {
        str += fmt::format("fn {}({}) -> {} {{\n", name, params_str, ret_type);
    }
    for (const auto& def : locals_) {
        str += ind(1) + fmt::format("{}\n", *def);
    }
    for (const auto& block : blocks_) {
        str += fmt::format("{}", *block);
    }
    str += "}\n";
    return str;
}

auto Func::locals() const -> const std::vector<std::unique_ptr<Alloc>>& {
    return locals_;
}
auto Func::locals() -> std::vector<std::unique_ptr<Alloc>>& {
    return locals_;
}

auto Func::blocks() const -> const std::vector<std::unique_ptr<Block>>& {
    return blocks_;
}
auto Func::blocks() -> std::vector<std::unique_ptr<Block>>& {
    return blocks_;
}

auto Func::newTemp(const Type& type, Block* container) -> TempValue {
    auto temp = TempValue{.type = type, .id = temps_.size(), .func = this};
    temps_.emplace_back(TempInfo{type, container});
    return temp;
}

auto Func::newBlock(const std::string& label) -> Block* {
    blocks_.emplace_back(std::make_unique<Block>(label));
    return blocks_.back().get();
}

auto Func::newBlock() -> Block* {
    return newBlock(fmt::format("L{}", temp_label_count++));
}

void Func::addBlock(std::unique_ptr<Block> block) {
    for (auto& b : blocks_) {
        if (b->label == block->label) {
            throw COMPILER_ERROR(fmt::format("block '{}' already exists", b->label));
        }
    }
    blocks_.push_back(std::move(block));
}

auto Func::findBlock(const std::string& label) const -> Block* {
    for (const auto& block : blocks_) {
        if (block->label == label) {
            return block.get();
        }
    }
    throw COMPILER_ERROR(fmt::format("block '{}' not found", label));
}

auto Func::findAlloc(const std::string& name) const -> const Alloc* {
    for (const auto& param : params) {
        if (param->name == name) return param.get();
    }
    for (const auto& local : locals_) {
        if (local->name == name) return local.get();
    }
    throw COMPILER_ERROR(fmt::format("alloc '{}' not found", name));
}

auto Func::entrance() const -> Block* {
    return blocks_.front().get();
}

void Func::addLocal(std::unique_ptr<Alloc> alloc) {
    locals_.push_back(std::move(alloc));
}

void Func::pushLoop(Block* continue_target, Block* break_target) {
    loops.push_back(LoopContext{continue_target, break_target});
}
void Func::popLoop() {
    loops.pop_back();
}
auto Func::currentLoop() const -> const LoopContext& {
    if (loops.empty()) {
        throw COMPILER_ERROR("No loop context available");
    }
    return loops.back();
}

auto Func::exits() const -> std::vector<Block*> {
    std::vector<Block*> exit_blocks;
    for (const auto& block : blocks_) {
        if (block->hasExit() && std::holds_alternative<ReturnExit>(block->exit())) {
            exit_blocks.push_back(block.get());
        }
    }
    return exit_blocks;
}

auto Func::hasRecursiveCall() const -> bool {
    for (auto& block : blocks_) {
        for (auto& inst : block->insts()) {
            if (auto call = std::get_if<CallInst>(&inst)) {
                if (call->func.def == NameDef{this}) {
                    return true;
                }
            }
        }
    }
    return false;
}

auto Func::numInsts() const -> size_t {
    size_t count = 0;
    for (auto& block : blocks_) {
        count += block->insts().size() + 1;
    }
    return count;
}

auto Func::split(Block* block, std::list<Inst>::iterator next_start, Exit prev_exit,
                 std::string next_label) -> std::unique_ptr<Block> {
    std::unique_ptr<Block> next = std::make_unique<Block>(std::move(next_label));
    next->insts().splice(next->insts().end(), block->insts(), next_start, block->insts().end());
    next->setExit(std::move(block->exit()));
    block->exit() = std::move(prev_exit);
    for (auto source_ref : analysis::utils::sources(*this)) {
        auto& source = source_ref.get();
        if (source == block) {
            source = next.get();
        }
    }
    return next;
}

auto Func::clone(const std::string& prefix) const -> std::unique_ptr<Func> {
    std::vector<std::unique_ptr<Alloc>> new_params, new_locals;
    new_params.reserve(params.size());
    new_locals.reserve(locals_.size());
    std::unordered_map<const Alloc*, const Alloc*> alloc_map;
    for (auto& param : params) {
        new_params.push_back(param->clone(prefix));
        alloc_map[param.get()] = new_params.back().get();
    }
    for (auto& local : locals_) {
        new_locals.push_back(local->clone(prefix));
        alloc_map[local.get()] = new_locals.back().get();
    }

    auto func = std::make_unique<Func>(ret_type, prefix + name, std::move(new_params));
    std::unordered_map<Block*, Block*> block_map;
    for (auto& block : blocks_) {
        auto new_block = block->clone(prefix);
        block_map[block.get()] = new_block.get();
        func->blocks_.push_back(std::move(new_block));
    }
    for (auto& [type, block] : temps_) {
        func->newTemp(type, block_map[block]);
    }

    for (auto var : analysis::utils::vars(*func)) {
        if (auto alloc_opt = analysis::utils::alloc_of(*var); alloc_opt) {
            auto& alloc = alloc_opt->get();
            if (alloc_map.count(alloc)) {
                alloc = alloc_map[alloc];
            }
        }
        if (auto temp = std::get_if<TempValue>(var)) {
            temp->func = func.get();
        }
    }
    for (auto& target_ref : analysis::utils::labels(*func)) {
        target_ref.get() = block_map[target_ref.get()];
    }
    func->locals() = std::move(new_locals);
    return func;
}

}  // namespace ir
