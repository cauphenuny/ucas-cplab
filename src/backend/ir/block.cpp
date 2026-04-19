#include "backend/ir/ir.h"

#include <cstddef>
#include <cstring>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ir {
auto Block::toString() const -> std::string {
    std::string str;
    str += ind(1) + fmt::format("'{}: {{\n", label);
    for (const auto& inst : insts_) {
        str += ind(2) + fmt::format("{}\n", inst);
    }
    str += ind(2) + (exit_ ? fmt::format("{}\n", *exit_) : fmt::format("<noexit>\n"));
    str += ind(1) + "}\n";
    return str;
}

void Block::add(Inst inst) {
    insts_.push_back(std::move(inst));
}
void Block::prepend(Inst inst) {
    insts_.insert(insts_.begin(), std::move(inst));
}
auto Block::pop_front() -> Inst {
    Inst inst = std::move(insts_.front());
    insts_.pop_front();
    return inst;
}

void Block::setExit(Exit exit) {
    if (this->exit_) {
        throw COMPILER_ERROR(fmt::format("Block {} already has an exit instruction", label));
    }
    this->exit_ = std::move(exit);
}

bool Block::hasExit() const {
    return exit_.has_value();
}
const Exit& Block::exit() const {
    return *exit_;
}
Exit& Block::exit() {
    return *exit_;
}

auto Block::clone(const std::string& prefix) -> std::unique_ptr<Block> {
    auto block = std::make_unique<Block>(prefix + label);
    for (auto& inst : insts_) {
        block->add(inst);
    }
    if (exit_) {
        block->setExit(*exit_);
    }
    return block;
}

Block::Block(std::string label, std::list<Inst> insts, Exit exit)
    : label(std::move(label)), insts_(std::move(insts)), exit_(std::move(exit)) {}

Block::Block(std::string label) : label(std::move(label)) {}

}  // namespace ir