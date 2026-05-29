/// Test RegToMem pass: spill non-ref Allocs that cannot be promoted to
/// registers (globals, params exceeding register count) to ref Allocs,
/// inserting LOAD/STORE instructions so that accesses go through memory.
/// Uses the RV64 ABI for parameter register assignment.

#include "backend/ir/ir.h"
#include "backend/ir/lowering/reg2mem.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/ir/vm/vm.h"
#include "backend/rv64/abi.hpp"
#include "fmt/base.h"

#include <functional>
#include <sstream>
#include <string>

using namespace ir;
using namespace ir::transform;

static int tests_passed = 0;
static int tests_failed = 0;

void check(bool cond, const std::string& msg) {
    if (cond) {
        fmt::println("    [OK] {}", msg);
        ++tests_passed;
    } else {
        fmt::println("    [FAIL] {}", msg);
        ++tests_failed;
    }
}

/// Parse non-SSA IR, apply RegToMem with RV64 ABI, then execute via VM and return the result.
int run(const std::string& ir_text) {
    auto stream = std::istringstream(ir_text);
    auto prog_box = ir::parse(stream);
    auto& prog = *prog_box;
    fmt::println("Before RegToMem:\n{}", prog);

    NonSSAPassContext ctx(prog);
    bool changed = RegToMem(rv64::ABI).apply(prog, ctx);

    fmt::println("After RegToMem (changed={}):\n{}", changed, prog);

    auto null_out = std::ostringstream{};
    auto vm = ir::vm::VirtualMachine(std::cin, null_out);
    return vm.execute(prog);
}

void test(const std::string& name, const std::string& ir_text, int expected) {
    fmt::println("Test: {}", name);
    try {
        int ret = run(ir_text);
        check(ret == expected, fmt::format("return value == {} (got {})", expected, ret));
    } catch (const std::exception& e) {
        fmt::println("  Error: {}", e.what());
        ++tests_failed;
    }
    fmt::println("------------------------------------------");
}

/// Run RegToMem and verify structural properties (does not use VM).
void test_structural(const std::string& name, const std::string& ir_text,
                     const std::function<void(Program&, bool changed)>& verify) {
    fmt::println("Test: {}", name);
    try {
        auto stream = std::istringstream(ir_text);
        auto prog_box = ir::parse(stream);
        auto& prog = *prog_box;
        fmt::println("Before structural:");
        fmt::println("{}", prog);

        NonSSAPassContext ctx(prog);
        bool changed = RegToMem(rv64::ABI).apply(prog, ctx);
        fmt::println("After structural (changed={}):", changed);
        fmt::println("{}", prog);

        verify(prog, changed);
    } catch (const std::exception& e) {
        fmt::println("  Error: {}", e.what());
        ++tests_failed;
    }
    fmt::println("------------------------------------------");
}

int main() {
    // ------------------------------------------------------------------
    // Test 1: Local variable NOT spilled (fits in register).
    // Workset is empty — IR unchanged. Expected: 42
    // ------------------------------------------------------------------
    test("Local NOT spilled", R"(
fn main() -> i32 {
    let mut x: i32;
    'entry: {
        @x: i32 = 42;
        return @x;
    }
}
)",
         42);

    // ------------------------------------------------------------------
    // Test 2: Multiple local variables.
    // Three locals @a, @b, @c — all should be spilled.
    // Expected: 10 + 20 = 30
    // ------------------------------------------------------------------
    test("Multiple locals", R"(
fn main() -> i32 {
    let mut a: i32;
    let mut b: i32;
    let mut c: i32;
    'entry: {
        @a: i32 = 10;
        @b: i32 = 20;
        %0: i32 = @a + @b;
        @c: i32 = %0;
        return @c;
    }
}
)",
         30);

    // ------------------------------------------------------------------
    // Test 3: Diamond control flow with locals.
    // Two locals modified in different branches, then merged.
    // Takes the 'then branch (condition is true).
    // Expected: x=11, y=2 → 13
    // ------------------------------------------------------------------
    test("Diamond control flow", R"(
fn main() -> i32 {
    let mut x: i32;
    let mut y: i32;
    'entry: {
        @x: i32 = 1;
        @y: i32 = 2;
        %0: bool = true;
        => if %0 { 'then } else { 'else_blk };
    }
    'then: {
        %1: i32 = @x + 10;
        @x: i32 = %1;
        => 'merge;
    }
    'else_blk: {
        %2: i32 = @y + 20;
        @y: i32 = %2;
        => 'merge;
    }
    'merge: {
        %3: i32 = @x + @y;
        return %3;
    }
}
)",
         13);

    // ------------------------------------------------------------------
    // Test 4: Loop with accumulator.
    // Computes sum = 0+1+2+3+4 = 10 using a for-like loop.
    // Both @sum and @i should be spilled.
    // Expected: 10
    // ------------------------------------------------------------------
    test("Loop accumulator", R"(
fn main() -> i32 {
    let mut sum: i32;
    let mut i: i32;
    'entry: {
        @sum: i32 = 0;
        @i: i32 = 0;
        => 'cond;
    }
    'cond: {
        %0: bool = @i < 5;
        => if %0 { 'body } else { 'exit };
    }
    'body: {
        %1: i32 = @sum + @i;
        @sum: i32 = %1;
        %2: i32 = @i + 1;
        @i: i32 = %2;
        => 'cond;
    }
    'exit: {
        return @sum;
    }
}
)",
         10);

    // ------------------------------------------------------------------
    // Test 5: No spill needed (only temps, no NamedValue allocs).
    // When all allocs are already references or there are no non-ref allocs,
    // workset is empty and apply() returns false.
    // ------------------------------------------------------------------
    test_structural("No spill needed (only temps, no locals)", R"(
fn main() -> i32 {
    'entry: {
        %0: i32 = 42;
        return %0;
    }
}
)",
                    [](Program& prog, bool changed) {
                        check(!changed, "apply returns false (no non-ref allocs to spill)");
                    });

    // ------------------------------------------------------------------
    // Test 6: Params beyond register count get spilled.
    // RV64 GPR has 8 parameter registers (x10-x17). A function with 9 int
    // params should spill the 9th param (index 8).
    // Uses structural check — no VM execution.
    // ------------------------------------------------------------------
    test_structural("9 int params — 9th spilled", R"(
fn many_params(a: i32, b: i32, c: i32, d: i32, e: i32, f: i32, g: i32, h: i32, i: i32) -> i32 {
    let mut x: i32;
    'entry: {
        %0: i32 = @a + @b;
        %1: i32 = %0 + @c;
        %2: i32 = %1 + @d;
        %3: i32 = %2 + @e;
        %4: i32 = %3 + @f;
        %5: i32 = %4 + @g;
        %6: i32 = %5 + @h;
        %7: i32 = %6 + @i;
        @x: i32 = %7;
        return @x;
    }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = 1;
        return %0;
    }
}
)",
                    [](Program& prog, bool changed) {
                        check(changed, "apply returns true (params exceeding regs need spilling)");
                        const auto& func = prog.findFunc("many_params");
                        check(func.params.size() == 9, "has 9 params");
                        // First 8 int params get registers, so they should NOT be spilled
                        for (size_t i = 0; i < 8; i++) {
                            check(!func.params[i]->reference,
                                  fmt::format("param {} NOT spilled (has register)", i));
                        }
                        // 9th param has no register → spilled
                        check(func.params[8]->reference, "9th param (index 8) IS spilled");
                    });

    // ------------------------------------------------------------------
    // Test 7: Mixed param types (int + float) within register limits.
    // All params fit in registers, local @r is kept in register.
    // Workset is empty — nothing to spill.
    // ------------------------------------------------------------------
    test_structural("Mixed params within register limits", R"(
fn mixed(a: i32, b: i32, x: f32, y: f32) -> i32 {
    let mut r: i32;
    'entry: {
        %0: i32 = @a + @b;
        @r: i32 = %0;
        return @r;
    }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = 1;
        return %0;
    }
}
)",
                    [](Program& prog, bool changed) {
                        check(!changed, "apply returns false (nothing to spill)");
                        const auto& func = prog.findFunc("mixed");
                        check(func.params.size() == 4, "has 4 params");
                        for (size_t i = 0; i < 4; i++) {
                            check(!func.params[i]->reference,
                                  fmt::format("param {} NOT spilled (has register)", i));
                        }
                    });

    // ------------------------------------------------------------------
    // Test 8: Global variable spill.
    // Non-ref globals should be added to the workset and spilled.
    // Uses `let` at program scope to declare a global variable.
    // ------------------------------------------------------------------
    test_structural("Global variable spill", R"(
let x: i32 = 100;
fn main() -> i32 {
    let mut y: i32;
    'entry: {
        %0: i32 = @x + 1;
        @y: i32 = %0;
        return @y;
    }
}
)",
                    [](Program& prog, bool changed) {
                        check(changed, "apply returns true (global needs spilling)");
                        auto* global = prog.findAlloc("x");
                        check(global != nullptr, "found global x");
                        if (global) {
                            check(global->reference, "global x is spilled (now reference)");
                        }
                    });

    // ------------------------------------------------------------------
    // Test 9: Multiple assignments to the same local.
    // @x is assigned twice — both assignments should become STOREs.
    // Expected: x=10, then x=20, return 20
    // ------------------------------------------------------------------
    test("Multiple assignments to same local", R"(
fn main() -> i32 {
    let mut x: i32;
    'entry: {
        @x: i32 = 10;
        @x: i32 = 20;
        return @x;
    }
}
)",
         20);

    // ------------------------------------------------------------------
    // Test 10: Boolean local.
    // A bool local should be spilled like any other primitive.
    // Expected: 1 (true branch taken)
    // ------------------------------------------------------------------
    test("Boolean local spill", R"(
fn main() -> i32 {
    let mut flag: bool;
    let mut result: i32;
    'entry: {
        @flag: bool = true;
        %0: bool = @flag;
        => if %0 { 'true_blk } else { 'false_blk };
    }
    'true_blk: {
        @result: i32 = 1;
        => 'done;
    }
    'false_blk: {
        @result: i32 = 0;
        => 'done;
    }
    'done: {
        return @result;
    }
}
)",
         1);

    fmt::println("\nResults: {} passed, {} failed", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
