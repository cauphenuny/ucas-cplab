#pragma once
#include "backend/ir/analysis/dataflow/framework.hpp"
#include "backend/ir/analysis/usedef.h"
#include "backend/ir/ir.h"

namespace ir::transform {

using namespace ir::analysis;

template <typename Context> struct Pass {
    virtual ~Pass() = default;
    virtual bool apply(Program& prog, Context& ctx) = 0;
};

template <> struct Pass<void> {
    virtual ~Pass() = default;
    virtual bool apply(Program& prog) = 0;
};

template <typename Context, typename... Passes> struct Compose : Pass<Context> {
    bool apply(Program& prog, Context& ctx) override {
        auto result = false;
        ((result |= Passes().apply(prog, ctx)), ...);
        return result;
    }
};

template <typename... Passes> struct Compose<void, Passes...> : Pass<void> {
    bool apply(Program& prog) override {
        auto result = false;
        ((result |= Passes().apply(prog)), ...);
        return result;
    }
};

struct SSAPassContext {
    UseDefGraph ud;
    SSAPassContext(Program& program) : ud(program) {}
};

struct NonSSAPassContext {
    NonSSAUseDefGraph ud;
    NonSSAPassContext(Program& program) : ud(program) {}
};

using SSAPass = Pass<SSAPassContext>;
using NonSSAPass = Pass<NonSSAPassContext>;

}  // namespace ir::transform
