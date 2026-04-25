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
