#pragma once
#include "backend/ir/ir.hpp"

namespace ir::optim::pass {

struct Pass {
    virtual ~Pass() = default;
    virtual void apply(Program& prog) = 0;
};

template <typename... Passes> struct Compose : Pass {
    void apply(Program& prog) override {
        (Passes().apply(prog), ...);
    }
};

}  // namespace ir::optim::pass
