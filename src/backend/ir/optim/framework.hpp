#pragma once
#include "backend/ir/analysis/dataflow/framework.hpp"
#include "backend/ir/ir.hpp"

namespace ir::optim {

using namespace ir::analysis;

struct Pass {
    virtual ~Pass() = default;
    virtual bool apply(Program& prog) = 0;
};

template <typename... Passes> struct Compose : Pass {
    bool apply(Program& prog) override {
        return (Passes().apply(prog) || ...);
    }
};

}  // namespace ir::optim
