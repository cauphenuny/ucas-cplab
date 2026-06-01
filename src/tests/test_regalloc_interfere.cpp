/// Test InterfereGraph::build: interference edges and caller-saved register forbidding
/// via global pre-color proxy allocs.
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
#include "backend/ir/lowering/regalloc/graph.hpp"
#include "backend/ir/lowering/regalloc/precolorize.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/rv64/abi.hpp"
#include "fmt/base.h"

#include <functional>
#include <sstream>
#include <string>
#include <utility>

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

using Graphs = std::pair<InterfereGraph, InterfereGraph>;

void test_interfere(
    const std::string& name, const std::string& ir_text,
    const std::function<void(Program&, Graphs&, Precolorize&)>& verify) {
    fmt::println("Test: {}", name);
    try {
        auto stream = std::istringstream(ir_text);
        auto prog_box = ir::parse(stream);
        auto& prog = *prog_box;

        Precolorize precolor(rv64::ABI);
        NonSSAPassContext ctx(prog);
        precolor.apply(prog, ctx);

        auto graphs = InterfereGraph::build(prog, precolor.precolored, rv64::ABI);
        verify(prog, graphs, precolor);
    } catch (const std::exception& e) {
        fmt::println("  Error: {}", e.what());
        ++tests_failed;
    }
    fmt::println("------------------------------------------");
}

LeftValue named(const Func& func, const std::string& name) {
    return LeftValue{func.findAlloc(name)->value()};
}

LeftValue temp(const Func& func, size_t id) {
    return LeftValue{TempValue{ir::type::construct<int>(), id, const_cast<Func*>(&func)}};
}

int main() {
    // Test 1: Basic interference — a ↔ b, neither interferes with %0.
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
                   [](Program& prog, Graphs& gs, Precolorize&) {
                       auto& g = gs.first;
                       auto& func = prog.findFunc("f");
                       auto a = named(func, "a");
                       auto b = named(func, "b");
                       auto t0 = temp(func, 0);
                       check(g.interferes(a, b), "a interferes with b");
                       check(g.interferes(b, a), "b interferes with a (symmetric)");
                       check(!g.interferes(a, t0), "a does NOT interfere with %0");
                       check(!g.interferes(b, t0), "b does NOT interfere with %0");
                   });

    // Test 2: Sequential chain — no interference.
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
                   [](Program& prog, Graphs& gs, Precolorize&) {
                       auto& g = gs.first;
                       auto& func = prog.findFunc("f");
                       auto a = named(func, "a");
                       auto b = named(func, "b");
                       check(!g.interferes(a, b), "a does NOT interfere with b");
                       check(!g.interferes(b, a), "b does NOT interfere with a");
                   });

    // Test 3: Three vars — only a ↔ b.
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
                   [](Program& prog, Graphs& gs, Precolorize&) {
                       auto& g = gs.first;
                       auto& func = prog.findFunc("f");
                       auto a = named(func, "a");
                       auto b = named(func, "b");
                       auto c = named(func, "c");
                       check(g.interferes(a, b), "a interferes with b");
                       check(!g.interferes(a, c), "a does NOT interfere with c");
                       check(!g.interferes(b, c), "b does NOT interfere with c");
                   });

    // Test 4: GPR caller-saved — var live across call.
    test_interfere("GPR caller-saved: var live across call → caller-saved proxies interfere",
                   R"(
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
                   [](Program& prog, Graphs& gs, Precolorize& precolor) {
                       auto& g = gs.first;
                       auto& func = prog.findFunc("f");
                       auto x = named(func, "x");
                       for (auto reg : rv64::abi::GPR.caller_saved) {
                           auto proxy_lv = LeftValue{
                               precolor.precolored.at({ir::type::construct<int>(), reg})->value()};
                           check(g.interferes(x, proxy_lv),
                                 fmt::format("x interferes with GPR caller-saved proxy (reg {})",
                                             reg));
                       }
                   });

    // Test 5: GPR caller-saved — var dead before call.
    test_interfere("GPR caller-saved: var dead before call → no caller-saved interference",
                   R"(
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
                   [](Program& prog, Graphs& gs, Precolorize& precolor) {
                       auto& g = gs.first;
                       auto& func = prog.findFunc("f");
                       auto x = named(func, "x");
                       for (auto reg : rv64::abi::GPR.caller_saved) {
                           auto proxy_lv = LeftValue{
                               precolor.precolored.at({ir::type::construct<int>(), reg})->value()};
                           check(!g.interferes(x, proxy_lv),
                                 fmt::format("x does NOT interfere with GPR caller-saved proxy "
                                             "(reg {}) — x dead before call",
                                             reg));
                       }
                   });

    // Test 6: FPR caller-saved — float var live across call.
    test_interfere("FPR caller-saved: float var live across call → FPR proxies interfere",
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
                   [](Program& prog, Graphs& gs, Precolorize& precolor) {
                       auto& g = gs.second;
                       auto& func = prog.findFunc("f");
                       auto v = named(func, "v");
                       for (auto reg : rv64::abi::FPR.caller_saved) {
                           auto proxy_lv = LeftValue{
                               precolor.precolored.at({ir::type::construct<double>(), reg})->value()};
                           check(g.interferes(v, proxy_lv),
                                 fmt::format("v interferes with FPR caller-saved proxy (reg {})",
                                             reg));
                       }
                   });

    // Test 7: Cross-block loop — loop-carried vars interfere.
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
                   [](Program& prog, Graphs& gs, Precolorize&) {
                       auto& g = gs.first;
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
