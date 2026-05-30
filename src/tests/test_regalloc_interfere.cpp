/// Test InterfereGraph::build: interference edges and caller-saved register forbidding.
///
/// Key behaviors tested:
///   1. Two variables simultaneously live at a def point → interfere.
///   2. Sequential chain (non-overlapping lifetimes) → no interference.
///   3. Three variables: only the simultaneously-live pair interferes.
///   4. GPR variable live across a call → forbidden from all GPR caller-saved regs.
///   5. GPR variable dead before a call → no caller-saved forbidden from that call.
///   6. FPR variable live across a call → forbidden from all FPR caller-saved regs.
///   7. Cross-block (loop): loop variables with overlapping live ranges interfere.

#include "backend/ir/ir.h"
#include "backend/ir/lowering/regalloc/interfere.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/rv64/abi.hpp"
#include "fmt/base.h"

#include <functional>
#include <set>
#include <sstream>
#include <string>

using namespace ir;
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

void test_interfere(const std::string& name, const std::string& ir_text,
                    const std::function<void(Program&, InterfereGraph&)>& verify) {
    fmt::println("Test: {}", name);
    try {
        auto stream = std::istringstream(ir_text);
        auto prog_box = ir::parse(stream);
        auto& prog = *prog_box;
        ColorMap empty_precolor;
        auto graph = InterfereGraph::build(prog, empty_precolor, rv64::ABI);
        verify(prog, graph);
    } catch (const std::exception& e) {
        fmt::println("  Error: {}", e.what());
        ++tests_failed;
    }
    fmt::println("------------------------------------------");
}

/// Get the LeftValue for a named allocation (param or local) in a function.
LeftValue named(const Func& func, const std::string& name) {
    return LeftValue{func.findAlloc(name)->value()};
}

/// Construct a TempValue LeftValue by id (equality uses id + func* only, type ignored).
LeftValue temp(const Func& func, size_t id) {
    return LeftValue{TempValue{ir::type::construct<int>(), id, const_cast<Func*>(&func)}};
}

int main() {
    // -----------------------------------------------------------------------
    // Test 1: Basic interference.
    //   @a = 1; @b = 2; %0 = @a + @b; return %0;
    // When @b is defined, @a is already live → a ↔ b.
    // Neither a nor b is live when %0 is defined (live = {%0} only) → no edge with %0.
    // -----------------------------------------------------------------------
    test_interfere("two vars simultaneously live → interfere", R"(
fn f() -> i32 {
    let mut a: i32;
    let mut b: i32;
    'entry: {
        @a: i32 = 1;
        @b: i32 = 2;
        %0: i32 = @a + @b;
        return %0;
    }
}
)",
                   [](Program& prog, InterfereGraph& g) {
                       auto& func = prog.findFunc("f");
                       auto a = named(func, "a");
                       auto b = named(func, "b");
                       auto t0 = temp(func, 0);
                       check(g.interferes(a, b), "a interferes with b");
                       check(g.interferes(b, a), "b interferes with a (symmetric)");
                       check(!g.interferes(a, t0), "a does NOT interfere with %0");
                       check(!g.interferes(b, t0), "b does NOT interfere with %0");
                   });

    // -----------------------------------------------------------------------
    // Test 2: Sequential chain — non-overlapping lifetimes, no interference.
    //   @a = 1; @b = @a + 1; return @b;
    // a's live range ends at the def of b → a NOT ↔ b.
    // -----------------------------------------------------------------------
    test_interfere("sequential chain → no interference", R"(
fn f() -> i32 {
    let mut a: i32;
    let mut b: i32;
    'entry: {
        @a: i32 = 1;
        @b: i32 = @a + 1;
        return @b;
    }
}
)",
                   [](Program& prog, InterfereGraph& g) {
                       auto& func = prog.findFunc("f");
                       auto a = named(func, "a");
                       auto b = named(func, "b");
                       check(!g.interferes(a, b), "a does NOT interfere with b");
                       check(!g.interferes(b, a), "b does NOT interfere with a");
                   });

    // -----------------------------------------------------------------------
    // Test 3: Three vars — only the pair alive at the same def point interferes.
    //   @a = 1; @b = 2; @c = @a + @b; return @c;
    // When @b is defined, @a is live → a ↔ b.
    // When @c is defined, live = {%0} only → c has no named-var neighbors.
    // -----------------------------------------------------------------------
    test_interfere("three vars: a↔b, c independent of both", R"(
fn f() -> i32 {
    let mut a: i32;
    let mut b: i32;
    let mut c: i32;
    'entry: {
        @a: i32 = 1;
        @b: i32 = 2;
        @c: i32 = @a + @b;
        return @c;
    }
}
)",
                   [](Program& prog, InterfereGraph& g) {
                       auto& func = prog.findFunc("f");
                       auto a = named(func, "a");
                       auto b = named(func, "b");
                       auto c = named(func, "c");
                       check(g.interferes(a, b), "a interferes with b");
                       check(!g.interferes(a, c), "a does NOT interfere with c");
                       check(!g.interferes(b, c), "b does NOT interfere with c");
                   });

    // -----------------------------------------------------------------------
    // Test 4: GPR caller-saved unavailable — var live across a call.
    //   @x = 5; %0 = @g(); %1 = @x + %0; return %1;
    // @x is in the live set when the CallInst is processed
    // → all GPR caller-saved regs removed from available_colors.
    // RV64 GPR caller-saved: {1, 5, 6, 7, 10, 11, 12, 13, 14, 15, 16, 17, 28, 29, 30, 31}
    // -----------------------------------------------------------------------
    test_interfere("GPR caller-saved: var live across call → caller-saved unavailable", R"(
fn g() -> i32 {
    'entry: {
        return 1;
    }
}
fn f() -> i32 {
    let mut x: i32;
    'entry: {
        @x: i32 = 5;
        %0: i32 = @g();
        %1: i32 = @x + %0;
        return %1;
    }
}
)",
                   [](Program& prog, InterfereGraph& g) {
                       auto& func = prog.findFunc("f");
                       auto x = named(func, "x");
                       const auto& available = g[x].available_colors;
                       for (auto reg : rv64::abi::GPR.caller_saved) {
                           check(available.count(reg) == 0,
                                 fmt::format("x: GPR caller-saved x{} unavailable", reg));
                       }
                   });

    // -----------------------------------------------------------------------
    // Test 5: GPR caller-saved still available — var dead before the call.
    //   @x = 5; @y = @x + 1; %0 = @g(); %1 = @y + %0; return %1;
    // @x's last use is in the def of @y, so @x is dead at the CallInst
    // → caller-saved regs should still be in available_colors.
    // -----------------------------------------------------------------------
    test_interfere("GPR caller-saved: var dead before call → caller-saved still available", R"(
fn g() -> i32 {
    'entry: {
        return 1;
    }
}
fn f() -> i32 {
    let mut x: i32;
    let mut y: i32;
    'entry: {
        @x: i32 = 5;
        @y: i32 = @x + 1;
        %0: i32 = @g();
        %1: i32 = @y + %0;
        return %1;
    }
}
)",
                   [](Program& prog, InterfereGraph& g) {
                       auto& func = prog.findFunc("f");
                       auto x = named(func, "x");
                       const auto& available = g[x].available_colors;
                       for (auto reg : rv64::abi::GPR.caller_saved) {
                           check(available.count(reg) > 0,
                                 fmt::format("x: GPR x{} still available (x dead before call)", reg));
                       }
                   });

    // -----------------------------------------------------------------------
    // Test 6: FPR caller-saved unavailable — float var live across a call.
    //   @v = 1.0; %0 = @h(); %1 = @v + %0; return %1;
    // @v (f64) is live at the call → all FPR caller-saved removed from available_colors.
    // RV64 FPR caller-saved: {0..7, 10..17, 28..31}
    // -----------------------------------------------------------------------
    test_interfere("FPR caller-saved: float var live across call → fpr caller-saved unavailable",
                   R"(
fn h() -> f64 {
    'entry: {
        return 0.0;
    }
}
fn f() -> f64 {
    let mut v: f64;
    'entry: {
        @v: f64 = 1.0;
        %0: f64 = @h();
        %1: f64 = @v + %0;
        return %1;
    }
}
)",
                   [](Program& prog, InterfereGraph& g) {
                       auto& func = prog.findFunc("f");
                       auto v = named(func, "v");
                       const auto& available = g[v].available_colors;
                       for (auto reg : rv64::abi::FPR.caller_saved) {
                           check(available.count(reg) == 0,
                                 fmt::format("v: FPR caller-saved f{} unavailable", reg));
                       }
                   });

    // -----------------------------------------------------------------------
    // Test 7: Cross-block interference in a loop.
    //   @i and @s are both live throughout the loop body → i ↔ s.
    //   @n (param) is live throughout → i ↔ n, s ↔ n.
    // -----------------------------------------------------------------------
    test_interfere("loop: loop-carried vars interfere", R"(
fn f(n: i32) -> i32 {
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
        %0: bool = @i < @n;
        => if %0 { 'loop } else { 'exit };
    }
    'exit: {
        return @s;
    }
}
)",
                   [](Program& prog, InterfereGraph& g) {
                       auto& func = prog.findFunc("f");
                       auto i = named(func, "i");
                       auto s = named(func, "s");
                       auto n = named(func, "n");
                       check(g.interferes(i, s), "i interferes with s (both live in loop)");
                       check(g.interferes(i, n), "i interferes with n (both live in loop)");
                       check(g.interferes(s, n), "s interferes with n (both live in loop)");
                   });

    fmt::println("\nResults: {} passed, {} failed", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
