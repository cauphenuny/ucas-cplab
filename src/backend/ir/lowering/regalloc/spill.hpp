#pragma once

#include "backend/ir/ir.h"
#include "backend/ir/lowering/reg2mem.hpp"
#include "backend/ir/transform/framework.hpp"
#include "utils/diagnosis.hpp"

#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace ir::lowering {

struct Spill : transform::NonSSAPass {
    Spill(std::vector<LeftValue> values, bool verbose = false) : values(std::move(values)), verbose(verbose) {}
    std::vector<LeftValue> values;
    bool verbose;
    bool apply(Program& prog, transform::NonSSAPassContext& ctx) override {
        if (values.empty()) return false;
        std::unordered_set<Alloc*> workset;
        for (const auto& value : values) {
            Match{value}([&](const SSAValue& ssa) { throw UNIMPLEMENTED_ERROR("spill SSA Value"); },
                         [&](const TempValue& temp) {
                             auto name = fmt::format("__spill_{}", temp.id);
                             auto alloc = Alloc::variable(name, temp.type);
                             ctx.ud.replace_all_defs_with(temp, alloc->value());
                             ctx.ud.replace_all_uses_with(temp, LeftValue{alloc->value()});
                             workset.insert(alloc.get());
                             temp.func->addLocal(std::move(alloc));
                         },
                         [&](const NamedValue& named) {
                             workset.insert(const_cast<Alloc*>(std::get<const Alloc*>(named.def)));
                         });
        }
        spill(workset, &prog, ctx);
        if (verbose) {
            fmt::println(stderr, "After spilling {}:\n{}", values, prog);
        }
        values.clear();
        return true;
    }
};

}  // namespace ir::lowering