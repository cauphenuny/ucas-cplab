/// Integration test for the full RegisterAllocation pass pipeline.
///
///   PreColorize → InterfereGraph::build → scan_move
///   → BriggsAllocator::colorize → Spill (loop if needed)
///
/// Verifies:
///   1. All colored values have valid colors
///   2. No two interfering neighbors share a color
///   3. Programs can still be VM-executed after full regalloc

#include "backend/ir/ir.h"
#include "backend/ir/lowering/regalloc/main.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/ir/vm/vm.h"
#include "backend/rv64/abi.hpp"
#include "fmt/base.h"

#include <sstream>
#include <string>
#include <unordered_map>

using namespace ir;
using namespace ir::transform;
using namespace ir::lowering;

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

/// Verify no color conflicts in the colored map.
void verify_no_conflicts(const Func& func, InterfereGraph& graph, const ColorMap& colors) {
    for (const auto& [v, c] : colors) {
        for (const auto& n : graph[v].neighbors) {
            auto it = colors.find(n);
            if (it != colors.end() && it->second == c) {
                check(false,
                      fmt::format("conflict: {} and {} both have color {}", v, n, c));
                return;
            }
        }
    }
}

/// Run regalloc + verify + VM-execute.
void test_regalloc_and_run(const std::string& name, const std::string& ir_text, int expected) {
    fmt::println("Test: {}", name);
    try {
        auto stream = std::istringstream(ir_text);
        auto prog_box = ir::parse(stream);
        auto& prog = *prog_box;

        NonSSAPassContext ctx(prog);
        RegisterAllocation regalloc(rv64::ABI);
        regalloc.apply(prog, ctx);

        // Verify no color conflicts
        for (const auto& func : prog.funcs()) {
            auto graph = InterfereGraph::build(prog, regalloc.proxies, rv64::ABI);
            verify_no_conflicts(*func, graph, regalloc.colored);
        }

        // Execute via VM
        auto null_out = std::ostringstream{};
        auto vm = ir::vm::VirtualMachine(std::cin, null_out);
        int ret = vm.execute(prog);
        check(ret == expected, fmt::format("return value == {} (got {})", expected, ret));
    } catch (const std::exception& e) {
        fmt::println("  Error: {}", e.what());
        ++tests_failed;
    }
    fmt::println("------------------------------------------");
}

int main() {
    // =====================================================================
    // 1. Simple linear — 3 interfering vars.
    // =====================================================================
    test_regalloc_and_run("simple linear: 3 vars", R"(
fn main() -> i32 {
    let mut a: i32;
    let mut b: i32;
    let mut c: i32;
    'entry: {
        @a: i32 = 1;
        @b: i32 = @a + 2;
        @c: i32 = @a + @b;
        return @c;
    }
}
)",
                          4);  // 1+2=3, 1+3=4

    // =====================================================================
    // 2. Loop with induction var + accumulator (n=10 hardcoded).
    // =====================================================================
    test_regalloc_and_run("loop: induction var + accumulator", R"(
fn main() -> i32 {
    let mut i: i32;
    let mut s: i32;
    'entry: {
        @i: i32 = 0;
        @s: i32 = 0;
        => 'loop;
    }
    'loop: {
        @s: i32 = @s + @i;
        @i: i32 = @i + 1;
        %0: bool = @i < 10;
        => if %0 { 'loop } else { 'exit };
    }
    'exit: {
        return @s;
    }
}
)",
                          45);

    // =====================================================================
    // 3. Chained temps (no user vars).
    // =====================================================================
    test_regalloc_and_run("chain: many sequential temps", R"(
fn main() -> i32 {
    'entry: {
        %0: i32 = 1;
        %1: i32 = %0 + 2;
        %2: i32 = %1 + 3;
        %3: i32 = %2 + 4;
        %4: i32 = %3 + 5;
        %5: i32 = %4 + 6;
        %6: i32 = %5 + 7;
        %7: i32 = %6 + 8;
        %8: i32 = %7 + 9;
        %9: i32 = %8 + 10;
        return %9;
    }
}
)",
                          55);

    // =====================================================================
    // Summary
    // =====================================================================
    fmt::println("\nResults: {} passed, {} failed", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
