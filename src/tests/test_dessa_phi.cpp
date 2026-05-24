/// Test ReplacePhi in ssa_destruct.hpp.
///
/// The key scenario is the "swap problem" described in Briggs et al. 1998,
/// Section "REPLACING phi-FUNCTIONS WITH COPIES", Figure 12:
///
///   a <- b; b <- a   (simultaneous swap)
///
/// In SSA form the loop header carries:
///   %a.1 = phi('entry: %a.0, 'body: %b.1)
///   %b.1 = phi('entry: %b.0, 'body: %a.1)
///
/// The back-edge copy set for 'body -> 'loop is { %b.1->%a.1, %a.1->%b.1 },
/// a cycle.  schedule_copy must detect the cycle and break it with a temp:
///
///   temp  = %a.1        (save old a)
///   %a.1  = %b.1        (a <- b)
///   %b.1  = temp        (b <- old a)
///
/// A naive (buggy) implementation inserts %b.1 = %b.1 (no-op), leaving both
/// variables with b's value after one iteration.

#include "backend/ir/ir.h"
#include "backend/ir/optim/framework.hpp"
#include "backend/ir/optim/ssa_destruct.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/ir/vm/vm.h"
#include "fmt/base.h"

#include <sstream>
#include <string>

using namespace ir;
using namespace ir::optim;

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

/// Parse SSA IR, apply DestructSSA, then execute via VM and return the
/// program's return value.
int run(const std::string& ir_text) {
    auto stream = std::istringstream(ir_text);
    auto prog_box = ir::parse(stream);
    auto& prog = *prog_box;
    fmt::println("Before DestructSSA:\n{}", prog);
    NonSSAPassContext ctx(prog);
    DestructSSA{}.apply(prog, ctx);
    fmt::println("After DestructSSA:\n{}", prog);
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

int main() {
    // ------------------------------------------------------------------
    // Test 1: Simple diamond — trivial non-cycling phi replacement.
    //
    //   entry
    //  /     \
    // left   right
    //  \     /
    //   merge:  %x.2 = phi('left: %x.0, 'right: %x.1)
    //
    // condition is `true` so we always take the left branch.
    // Expected return: 10
    // ------------------------------------------------------------------
    test("Diamond phi (no cycle, always-true branch)", R"(
fn main() -> i32 {
    let mut x: i32;
    'entry: {
        %0: bool = true;
        => if %0 { 'left } else { 'right };
    }
    'left: {
        %x.0: i32 = 10;
        => 'merge;
    }
    'right: {
        %x.1: i32 = 20;
        => 'merge;
    }
    'merge: {
        %x.2: i32 = phi('left: %x.0, 'right: %x.1);
        return %x.2;
    }
}
)",
         10);

    // ------------------------------------------------------------------
    // Test 2: Loop accumulator — phi replacement with back-edge copies,
    // no cycle.  Computes sum = 0+1+2+3+4 = 10.
    //
    // Back-edge copy set for 'body -> 'loop:
    //   { %sum.2 -> %sum.1,  %i.2 -> %i.1 }
    // Neither destination appears as a source of the other → no cycle.
    // Expected return: 10
    // ------------------------------------------------------------------
    test("Loop accumulator phi (no cycle)", R"(
fn main() -> i32 {
    let mut sum: i32;
    let mut i: i32;
    'entry: {
        %sum.0: i32 = 0;
        %i.0: i32 = 0;
        => 'loop;
    }
    'loop: {
        %sum.1: i32 = phi('entry: %sum.0, 'body: %sum.2);
        %i.1: i32 = phi('entry: %i.0, 'body: %i.2);
        %0: bool = %i.1 < 5;
        => if %0 { 'body } else { 'exit };
    }
    'body: {
        %sum.2: i32 = %sum.1 + %i.1;
        %i.2: i32 = %i.1 + 1;
        => 'loop;
    }
    'exit: {
        return %sum.1;
    }
}
)",
         10);

    // ------------------------------------------------------------------
    // Test 3: THE SWAP PROBLEM (Briggs 1998, Figure 12).
    //
    // The loop header has two phi functions whose back-edge arguments are
    // each other's results — a direct copy cycle:
    //
    //   %a.1 = phi('entry: %a.0, 'body: %b.1)   <- a <- old b
    //   %b.1 = phi('entry: %b.0, 'body: %a.1)   <- b <- old a
    //
    // Back-edge copy set (for 'body -> 'loop):
    //   { %b.1 -> %a.1,  %a.1 -> %b.1 }
    //
    // Both destinations appear as sources → neither enters the worklist
    // immediately.  schedule_copy must break the cycle with a temporary:
    //
    //   temp  = %a.1        (save)
    //   %a.1  = %b.1        (a <- b)
    //   %b.1  = temp        (b <- old a)
    //
    // Semantic: start (a=1, b=2), run ONE swap iteration, return a + b.
    //   Correct:  a=2, b=1  -> return 3
    //   Buggy:    a=2, b=2  -> return 4   (temp not used, b.1=b.1 is a NOP)
    // ------------------------------------------------------------------
    test("Swap problem (cycle: a.1<->b.1 in back-edge)", R"(
fn main() -> i32 {
    let mut a: i32;
    let mut b: i32;
    let mut cnt: i32;
    'entry: {
        %a.0: i32 = 1;
        %b.0: i32 = 2;
        %cnt.0: i32 = 0;
        => 'loop;
    }
    'loop: {
        %a.1: i32 = phi('entry: %a.0, 'body: %b.1);
        %b.1: i32 = phi('entry: %b.0, 'body: %a.1);
        %cnt.1: i32 = phi('entry: %cnt.0, 'body: %cnt.2);
        %0: bool = %cnt.1 < 1;
        => if %0 { 'body } else { 'exit };
    }
    'body: {
        %cnt.2: i32 = %cnt.1 + 1;
        => 'loop;
    }
    'exit: {
        %1: i32 = %a.1 + %b.1;
        return %1;
    }
}
)",
         3);  // correct: 2+1=3;  buggy (no temp): 2+2=4

    // ------------------------------------------------------------------
    // Test 4: Three-way cycle — a->b->c->a rotation.
    //
    //   %a.1 = phi('entry: %a.0, 'body: %c.1)   <- a <- old c
    //   %b.1 = phi('entry: %b.0, 'body: %a.1)   <- b <- old a
    //   %c.1 = phi('entry: %c.0, 'body: %b.1)   <- c <- old b
    //
    // Start (a=1, b=2, c=3), ONE rotation: a=3, b=1, c=2.
    // Return a - b = 3 - 1 = 2.
    // ------------------------------------------------------------------
    test("Three-way cycle (a->b->c->a rotation)", R"(
fn main() -> i32 {
    let mut a: i32;
    let mut b: i32;
    let mut c: i32;
    let mut cnt: i32;
    'entry: {
        %a.0: i32 = 1;
        %b.0: i32 = 2;
        %c.0: i32 = 3;
        %cnt.0: i32 = 0;
        => 'loop;
    }
    'loop: {
        %a.1: i32 = phi('entry: %a.0, 'body: %c.1);
        %b.1: i32 = phi('entry: %b.0, 'body: %a.1);
        %c.1: i32 = phi('entry: %c.0, 'body: %b.1);
        %cnt.1: i32 = phi('entry: %cnt.0, 'body: %cnt.2);
        %0: bool = %cnt.1 < 1;
        => if %0 { 'body } else { 'exit };
    }
    'body: {
        %cnt.2: i32 = %cnt.1 + 1;
        => 'loop;
    }
    'exit: {
        %1: i32 = %a.1 - %b.1;
        return %1;
    }
}
)",
         2);  // correct: a=3, b=1, a-b=2

    fmt::println("\nResults: {} passed, {} failed", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
