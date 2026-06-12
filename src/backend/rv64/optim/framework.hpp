#pragma once

#include "backend/rv64/inst.hpp"

namespace rv64::optim {

struct Pass {
    virtual ~Pass() = default;
    virtual bool apply(Module& mod) = 0;
};

}  // namespace rv64::optim