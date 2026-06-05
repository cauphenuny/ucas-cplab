// Test: callee-saved register save/restore ordering bug.
//
// Reproduces the bug seen with 060_scope.cact on rv64 ABI:
// when colorize_callee_saved iterates callee-saved regs in the wrong order,
// the backup variables for the "wrong" registers get spilled, and the remaining
// backups fail to coalesce with their target registers, producing a cascade of
// register-to-register moves instead of clean sw/lw pairs.
//
// The IR used here is the same structure as 060_scope.cact:
//   - func(): a leaf function (no calls), needs callee-saved save/restore
//   - main(): calls func() in a loop, keeps live vars across the call

#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/lowering/regalloc/main.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/rv64/abi.hpp"

#include <sstream>
#include <string>

using namespace ir::lowering;
using namespace ir::transform;

// Same IR structure as 060_scope.cact
static const char* code = R"(
fn func() -> i32 {
    let mut b_0: i32;
    let mut a_1: i32;
    'entry: {
        @b_0: i32 = 0;
        @a_1: i32 = 1;
        %1: bool = @a_1 == @b_0;
        => if %1 { 'if_true } else { 'if_false };
    }
    'if_true: {
        %2: i32 = @a_1 + 1;
        @a_1: i32 = %2;
        return 1;
    }
    'if_false: {
        return 0;
    }
}

fn main() -> i32 {
    let mut result_0: i32;
    let mut i_0: i32;
    'entry: {
        @result_0: i32 = 0;
        @i_0: i32 = 0;
        => 'while_cond;
    }
    'while_cond: {
        %0: bool = @i_0 < 100;
        => if %0 { 'while_body } else { 'while_exit };
    }
    'while_body: {
        %1: i32 = @func();
        %2: bool = %1 == 1;
        => if %2 { 'if_true } else { 'if_exit };
    }
    'while_exit: {
        %5: bool = @result_0 < 100;
        => if %5 { 'print_one } else { 'print_zero };
    }
    'if_true: {
        %3: i32 = @result_0 + 1;
        @result_0: i32 = %3;
        => 'if_exit;
    }
    'if_exit: {
        %4: i32 = @i_0 + 1;
        @i_0: i32 = %4;
        => 'while_cond;
    }
    'print_one: {
        return 1;
    }
    'print_zero: {
        return 0;
    }
}
)";

int main() {
    auto stream = std::istringstream(code);
    auto prog_box = ir::parse(stream);
    auto& prog = *prog_box;
    NonSSAPassContext ctx(prog);
    // Use the real rv64 ABI — this is where the bug manifests
    RegisterAllocation regalloc(rv64::ABI, true);
    regalloc.apply(prog, ctx);
    ctx.ud.verify();
    fmt::println("=== After Register Allocation ===\n{}", prog);
    RegisterReplacement<NonSSAPassContext>(regalloc.colored, regalloc.precolored).apply(prog, ctx);
    RedundantMoveElimination<NonSSAPassContext>().apply(prog, ctx);
    ctx.ud.verify();
    fmt::println("=== After Redundant Move Elimination ===\n{}", prog);

    // Check for cascade moves: in main's entry, we should NOT see patterns like
    // @__reg_sN = @__reg_sM (register-to-register move between callee-saved regs).
    // If such moves exist, the coalescing failed and the bug is present.
    auto ir_str = fmt::format("{}", prog);
    bool has_cascade = ir_str.find("@__reg_s11: int = @__reg_s") != std::string::npos ||
                       ir_str.find("@__reg_s10: int = @__reg_s") != std::string::npos ||
                       ir_str.find("@__reg_s0: int = @__reg_ra") != std::string::npos;
    if (has_cascade) {
        fmt::println(stderr, "BUG: cascade callee-saved moves detected!");
        return 1;
    }
    fmt::println("OK: no cascade moves");
    return 0;
}
