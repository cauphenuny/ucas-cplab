/// Minimal example of inverted register allocation.
///
/// Pseudo-spill: callee-saved (s) register used as program variable, its
/// backup forced into a caller-saved (t) register — no stack spill.
///
/// ABI: 16 regs (t0..t13 caller, s0..s1 callee), 7 param regs (t0..t6).
/// IR: fn f with 7 params + 13 loop-carried vars = 20 values > 14 caller regs.

#include "backend/ir/lowering/regalloc/main.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/ir/transform/ssa/construct.hpp"
#include "backend/ir/transform/ssa/destruct.hpp"
#include "backend/rv64/abi.hpp"

#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace ir::lowering;
using namespace ir::transform;

int main() {
    // ABI: 16 regs, 14 caller (t0..t13), 2 callee (s0..s1), 7 params (t0..t6)
    // Minimum for simple-loop pseudo-spill. Param phis coalesce into param
    // regs; only the CONST-initialized vars compete for remaining colors.
    // With 14 caller regs and 7 params: 7 non-param phi colors needed.
    // 7+2(gen_backup)+2(float_backup)=11 < 16(max_color) -> no spill.
    std::set<size_t> caller, callee;
    for (int i = 0; i < 14; i++) caller.insert(i);
    for (int i = 14; i < 16; i++) callee.insert(i);
    std::vector<size_t> params = {0, 1, 2, 3, 4, 5, 6};

    RegisterABI regs = {
        .size = 16,
        .caller_saved = caller,
        .callee_saved = callee,
        .reserved = {},
        .parameters = params,
        .return_value = 0,
        .name = [](size_t idx) -> std::string {
            if (idx < 14) return fmt::format("t{}", idx);
            return fmt::format("s{}", idx - 14);
        },
    };
    RegisterABI floats = regs;
    floats.name = [](size_t) { return "f"; };
    TargetABI abi = {{regs, floats, 15}, rv64::ABI.mem};

    // IR: 7 params, 13 loop-carried vars (20 values > 14 caller regs)
    // 7 param-carrying phis coalesce; 6 const-initialized phis compete.
    // Coloring order + ok_colors.begin() non-determinism pushes the last
    // 2 const phis into s-regs, forcing their backups into t-regs.
    const char* ir_text = R"(
fn f(a: i32, b: i32, c: i32, d: i32, e: i32, f: i32, g: i32) -> i32 {
    let mut v1: i32;
    let mut v2: i32;
    let mut v3: i32;
    let mut v4: i32;
    let mut v5: i32;
    let mut v6: i32;
    let mut v7: i32;
    let mut v8: i32;
    let mut v9: i32;
    let mut v10: i32;
    let mut v11: i32;
    let mut v12: i32;
    let mut v13: i32;
    'entry: {
        @v1: i32 = @a;
        @v2: i32 = @b;
        @v3: i32 = @c;
        @v4: i32 = @d;
        @v5: i32 = @e;
        @v6: i32 = @f;
        @v7: i32 = @g;
        @v8: i32 = 0;
        @v9: i32 = 0;
        @v10: i32 = 0;
        @v11: i32 = 0;
        @v12: i32 = 0;
        @v13: i32 = 0;
        => 'cond;
    }
    'cond: {
        %0: bool = @v1 > 0;
        => if %0 { 'loop } else { 'exit };
    }
    'loop: {
        @v1: i32 = @v1 + 1;
        @v2: i32 = @v2 + 1;
        @v3: i32 = @v3 + 1;
        @v4: i32 = @v4 + 1;
        @v5: i32 = @v5 + 1;
        @v6: i32 = @v6 + 1;
        @v7: i32 = @v7 + 1;
        @v8: i32 = @v8 + 1;
        @v9: i32 = @v9 + 1;
        @v10: i32 = @v10 + 1;
        @v11: i32 = @v11 + 1;
        @v12: i32 = @v12 + 1;
        @v13: i32 = @v13 + 1;
        => 'cond;
    }
    'exit: { return @v1; }
}
)";

    // Parse + SSA pipeline
    auto stream = std::istringstream(ir_text);
    auto prog = ir::parse(stream);
    ConstructSSA().apply(*prog);
    NonSSAPassContext ctx(*prog);
    DestructSSA().apply(*prog, ctx);

    fmt::println("=== Before register allocation ===");
    fmt::println("{}", *prog);

    // Register allocation with verbose=true
    RegisterAllocation regalloc(abi, true);
    regalloc.apply(*prog, ctx);

    fmt::println("=== After allocation + replacement ===");
    RegisterReplacement<NonSSAPassContext>(regalloc.colored, regalloc.precolored).apply(*prog, ctx);
    RedundantMoveElimination<NonSSAPassContext>().apply(*prog, ctx);
    fmt::println("{}", *prog);

    // Print color map
    fmt::println("=== Color map ===");
    for (auto& [v, id] : regalloc.colored) fmt::println("  {} -> color {}", v, id);

    return 0;
}
