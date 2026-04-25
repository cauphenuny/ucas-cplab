#include "backend/ir/ir.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ir {

auto Program::toString() const -> std::string {
    std::string str;
    for (const auto& def : globals_) {
        str += fmt::format("{}\n", *def);
    }
    if (!str.empty()) str += "\n";
    for (const auto& func : funcs_) {
        str += fmt::format("{}\n", *func);
    }
    while (!str.empty() && str.back() == '\n') str.pop_back();  // remove last newlines
    return str;
}

void Program::addCallback(Callback* callback) {
    if (std::find(callbacks_.begin(), callbacks_.end(), callback) == callbacks_.end()) {
        callbacks_.push_back(callback);
    }
}

void Program::removeCallback(Callback* callback) {
    auto it = std::find(callbacks_.begin(), callbacks_.end(), callback);
    if (it != callbacks_.end()) {
        callbacks_.erase(it);
    }
}

void Program::after_inst_add(Block* block, Inst* it) {
    for (auto* callback : callbacks_) {
        callback->after_inst_add(block, it);
    }
}

void Program::before_inst_erase(Block* block, Inst* it) {
    for (auto* callback : callbacks_) {
        callback->before_inst_erase(block, it);
    }
}

void Program::after_exit_add(Block* block) {
    for (auto* callback : callbacks_) {
        callback->after_exit_add(block);
    }
}

void Program::before_exit_erase(Block* block) {
    for (auto* callback : callbacks_) {
        callback->before_exit_erase(block);
    }
}

void Program::after_block_add(Func* func, Block* block) {
    for (auto* callback : callbacks_) {
        callback->after_block_add(func, block);
    }
}

void Program::before_block_erase(Func* func, Block* block) {
    for (auto* callback : callbacks_) {
        callback->before_block_erase(func, block);
    }
}

void Program::addFunc(std::unique_ptr<Func> func) {
    funcs_.push_back(std::move(func));
}

void Program::addGlobal(std::unique_ptr<Alloc> alloc) {
    globals_.push_back(std::move(alloc));
}

void Program::addBuiltinFunc(std::unique_ptr<BuiltinFunc> func) {
    builtin_funcs_.push_back(std::move(func));
}

const Func& Program::findFunc(const std::string& name) const {
    for (const auto& func : funcs_) {
        if (func->name == name) {
            return *func;
        }
    }
    throw COMPILER_ERROR(fmt::format("function '{}' not found", name));
}

auto Program::findAlloc(const std::string& name) const -> const Alloc* {
    for (const auto& global : globals_) {
        if (global->name == name) return global.get();
    }
    throw COMPILER_ERROR(fmt::format("global alloc '{}' not found", name));
}

std::vector<std::unique_ptr<Alloc>>& Program::globals() {
    return globals_;
}

const std::vector<std::unique_ptr<Alloc>>& Program::globals() const {
    return globals_;
}

const std::vector<std::unique_ptr<Func>>& Program::funcs() const {
    return funcs_;
}

std::vector<std::unique_ptr<Func>>& Program::funcs() {
    return funcs_;
}

const std::vector<std::unique_ptr<BuiltinFunc>>& Program::builtins() const {
    return builtin_funcs_;
}

}  // namespace ir
