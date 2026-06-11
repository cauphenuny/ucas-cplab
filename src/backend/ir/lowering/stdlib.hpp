#pragma once

#include "backend/ir/ir.h"
#include "backend/ir/transform/framework.hpp"
#include "backend/ir/type.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ir::lowering {

std::vector<std::pair<std::string, Type>> stdlibs = {
    {
        "memcpy",
        type::construct<void(int64_t, int64_t, int64_t)>(),
    },
    {
        "memset",
        type::construct<void(int64_t, int64_t, int64_t)>(),
    },
};

template <typename T> struct AddStandardLib : transform::Pass<T> {
    bool apply(Program& program, T& ctx) override {
        for (auto& [name, type] : stdlibs) {
            program.addBuiltinFunc(std::make_unique<BuiltinFunc>(name, type));
        }
        return true;
    }
};

}  // namespace ir::lowering
