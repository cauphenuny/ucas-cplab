/// Test PreColorize pass: global pre-colored register proxies.
///
/// map_registers creates 64 global proxy allocs (shared across all functions):
///   32 integer-type GPR  (__reg_{name})
///   32 floating-type FPR (__reg_f{id})
///
/// All 64 are pre-colored to their physical register numbers.
/// Params, call-args, and returns reference these global proxies via MOV.

#include "backend/ir/ir.h"
#include "backend/ir/lowering/regalloc/precolorize.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/rv64/abi.hpp"
#include "fmt/base.h"

#include <array>
#include <functional>
#include <sstream>
#include <string>
#include <variant>

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

/// Derive a ColorMap from the PrecolorVars.
ColorMap build_color_map(const PrecolorVars& precolored) {
    ColorMap colors;
    for (const auto& [key, alloc] : precolored) {
        colors[alloc->value()] = static_cast<ssize_t>(key.second);
    }
    return colors;
}

size_t count_movs(const Block& block) {
    size_t count = 0;
    for (const auto& inst : block.insts()) {
        if (auto* unary = std::get_if<UnaryInst>(&inst)) {
            if (unary->op == UnaryInstOp::MOV) count++;
        }
    }
    return count;
}

/// Find a global alloc by name. Returns nullptr if not found.
const Alloc* find_global(const Program& prog, const std::string& name) {
    for (const auto& a : prog.globals())
        if (a->name == name) return a.get();
    return nullptr;
}

/// Verify all 64 global proxy allocs exist with correct register numbers.
void verify_all_64(const Program& prog, const PrecolorVars& precolored) {
    auto colors = build_color_map(precolored);
    check(colors.size() == 64, "colors has 64 entries (2 types × 32 regs)");
    check(prog.globals().size() == 64, "program has 64 global allocs");

    constexpr std::array gpr_names{
        "zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
        "s0",   "s1", "a0", "a1", "a2", "a3", "a4", "a5",
        "a6",   "a7", "s2", "s3", "s4", "s5", "s6", "s7",
        "s8",   "s9", "s10","s11","t3", "t4", "t5", "t6"
    };

    // 32 integer-type GPR proxies
    for (size_t idx = 0; idx < 32; idx++) {
        auto name = fmt::format("__reg_{}", gpr_names[idx]);
        auto* alloc = find_global(prog, name);
        check(alloc != nullptr, fmt::format("{} exists", name));
        if (alloc)
            check(colors.at(LeftValue{alloc->value()}) == static_cast<ssize_t>(idx),
                  fmt::format("{} -> register {}", name, idx));
    }
    // 32 floating-type FPR proxies
    for (size_t idx = 0; idx < 32; idx++) {
        auto name = fmt::format("__reg_f{}", idx);
        auto* alloc = find_global(prog, name);
        check(alloc != nullptr, fmt::format("{} exists", name));
        if (alloc)
            check(colors.at(LeftValue{alloc->value()}) == static_cast<ssize_t>(idx),
                  fmt::format("{} -> register {}", name, idx));
    }
}

void test_precolorize(const std::string& name, const std::string& ir_text,
                      const std::function<void(Program&, Precolorize&)>& verify) {
    fmt::println("Test: {}", name);
    try {
        auto stream = std::istringstream(ir_text);
        auto prog_box = ir::parse(stream);
        auto& prog = *prog_box;
        fmt::println("Before PreColorize:\n{}", prog);

        Precolorize pass(rv64::ABI);
        NonSSAPassContext ctx(prog);
        pass.apply(prog, ctx);

        fmt::println("After PreColorize:\n{}", prog);

        verify(prog, pass);
    } catch (const std::exception& e) {
        fmt::println("  Error: {}", e.what());
        ++tests_failed;
    }
    fmt::println("------------------------------------------");
}

int main() {
    // ==================================================================
    // Global proxy basics -- 64 entries regardless of function count.
    // ==================================================================

    test_precolorize("1 function = 64 global entries", R"(
fn singleton() -> i32 {
    'entry: { return 1; }
}
)",
                     [](Program& prog, Precolorize& pass) {
                         check(pass.precolored.proxies.size() == 64, "64 proxies (2 types x 32 regs)");
                         check(prog.globals().size() == 64, "64 global allocs");
                         verify_all_64(prog, pass.precolored);
                     });

    test_precolorize("2 functions = 64 global entries", R"(
fn add(a: i32, b: i32) -> i32 {
    'entry: {
        %0: i32 = @a + @b;
        return %0;
    }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = @add(1, 2);
        return %0;
    }
}
)",
                     [](Program& prog, Precolorize& pass) {
                         check(pass.precolored.proxies.size() == 64,
                               "64 proxies (global, not per-function)");
                         check(prog.globals().size() == 64, "64 global allocs");
                         verify_all_64(prog, pass.precolored);

                         auto* entry = prog.findFunc("main").findBlock("entry");
                         check(count_movs(*entry) >= 2, ">=2 MOVs before call (arg proxies)");
                     });

    test_precolorize("3 functions = 64 global entries", R"(
fn f1(x: i32) -> i32 { 'entry: { return @x; } }
fn f2(x: i32) -> i32 { 'entry: { return @x; } }
fn f3(x: i32) -> i32 { 'entry: { return @x; } }
)",
                     [](Program& prog, Precolorize& pass) {
                         check(pass.precolored.proxies.size() == 64,
                               "64 proxies regardless of function count");
                     });

    // ==================================================================
    // Colorize behavior -- same 64 globals reused across all functions.
    // ==================================================================

    test_precolorize("9 int args -- 9th spills", R"(
fn many(a: i32, b: i32, c: i32, d: i32, e: i32, f: i32, g: i32, h: i32, i: i32) -> i32 {
    'entry: {
        %0: i32 = @a + @b;
        return %0;
    }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = @many(1, 2, 3, 4, 5, 6, 7, 8, 9);
        return %0;
    }
}
)",
                     [](Program& prog, Precolorize& pass) {
                         check(pass.precolored.proxies.size() == 64, "64 proxies");
                         auto* entry = prog.findFunc("main").findBlock("entry");
                         check(count_movs(*entry) == 10,
                               "10 MOVs: 8 call arg MOVs + 1 retval MOV + 1 return MOV");
                     });

    test_precolorize("float (f32) args", R"(
fn fadd(a: f32, b: f32) -> f32 {
    'entry: {
        %0: f32 = @a + @b;
        return %0;
    }
}
fn main() -> f32 {
    'entry: {
        %0: f32 = @fadd(1.0, 2.0);
        return %0;
    }
}
)",
                     [](Program& prog, Precolorize& pass) {
                         check(pass.precolored.proxies.size() == 64, "64 proxies");
                         verify_all_64(prog, pass.precolored);
                     });

    test_precolorize("Mixed int+float args", R"(
fn mixed(a: i32, x: f32, b: i32, y: f32) -> i32 {
    'entry: {
        %0: i32 = @a + @b;
        return %0;
    }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = @mixed(1, 1.5, 2, 2.5);
        return %0;
    }
}
)",
                     [](Program& prog, Precolorize& pass) {
                         check(pass.precolored.proxies.size() == 64, "64 proxies");
                         verify_all_64(prog, pass.precolored);

                         auto* entry = prog.findFunc("main").findBlock("entry");
                         check(count_movs(*entry) >= 4, ">=4 MOVs (one per arg)");
                     });

    test_precolorize("Bool args", R"(
fn logic(a: bool, b: bool) -> bool {
    'entry: {
        %0: bool = @a && @b;
        return %0;
    }
}
fn main() -> i32 {
    'entry: {
        %0: bool = @logic(true, false);
        %1: i32 = %0;
        return %1;
    }
}
)",
                     [](Program& prog, Precolorize& pass) {
                         check(pass.precolored.proxies.size() == 64, "64 proxies");
                         verify_all_64(prog, pass.precolored);
                     });

    // ==================================================================
    // Return value colorization.
    // ==================================================================

    test_precolorize("Return value (i32) -- MOV to __reg_a0", R"(
fn answer() -> i32 {
    'entry: { return 42; }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = 1;
        return %0;
    }
}
)",
                     [](Program& prog, Precolorize& pass) {
                         check(pass.precolored.proxies.size() == 64, "64 proxies");
                         auto* ae = prog.findFunc("answer").entrance();
                         check(count_movs(*ae) == 1, "1 MOV in answer (return -> __reg_a0)");
                         auto* me = prog.findFunc("main").entrance();
                         check(count_movs(*me) == 2, "2 MOVs in main (temp %0=1 + return -> __reg_a0)");
                     });

    test_precolorize("Return value (f64)", R"(
fn pi() -> f64 {
    'entry: { return 3.14159; }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = 1;
        return %0;
    }
}
)",
                     [](Program& prog, Precolorize& pass) {
                         check(pass.precolored.proxies.size() == 64, "64 proxies");
                     });

    test_precolorize("No return exit", R"(
fn no_ret() -> i32 {
    'entry: { => 'entry; }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = 1;
        return %0;
    }
}
)",
                     [](Program& prog, Precolorize& pass) {
                         check(pass.precolored.proxies.size() == 64, "64 proxies");
                         check(count_movs(*prog.findFunc("no_ret").entrance()) == 0,
                               "no_ret has 0 MOVs");
                         check(count_movs(*prog.findFunc("main").entrance()) == 2,
                               "main has 2 MOVs");
                     });

    test_precolorize("Multiple calls -- proxies shared", R"(
fn square(x: i32) -> i32 {
    'entry: {
        %0: i32 = @x * @x;
        return %0;
    }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = @square(3);
        %1: i32 = @square(4);
        %2: i32 = %0 + %1;
        return %2;
    }
}
)",
                     [](Program& prog, Precolorize& pass) {
                         check(pass.precolored.proxies.size() == 64, "64 proxies");
                         auto* entry = prog.findFunc("main").findBlock("entry");
                         check(count_movs(*entry) >= 2, ">=2 MOVs (arg proxies)");
                     });

    // ==================================================================
    // Call args are NamedValue from global proxy.
    // ==================================================================

    test_precolorize("Call args are NamedValue (global proxy)", R"(
fn add(a: i32, b: i32) -> i32 {
    'entry: {
        %0: i32 = @a + @b;
        return %0;
    }
}
fn main() -> i32 {
    let mut x: i32;
    'entry: {
        @x: i32 = 10;
        %0: i32 = @x + 5;
        %1: i32 = @add(%0, @x);
        return %1;
    }
}
)",
                     [](Program& prog, Precolorize& pass) {
                         check(pass.precolored.proxies.size() == 64, "64 proxies");
                         auto* entry = prog.findFunc("main").findBlock("entry");
                         bool found_call = false;
                         for (auto& inst : entry->insts()) {
                             if (auto* call = std::get_if<CallInst>(&inst)) {
                                 found_call = true;
                                 for (auto& arg : call->args) {
                                     auto* lv = std::get_if<LeftValue>(&arg);
                                     check(lv && std::holds_alternative<NamedValue>(*lv),
                                           "call arg is a NamedValue (global proxy)");
                                 }
                             }
                         }
                         check(found_call, "found call instruction");
                     });

    // ==================================================================
    // Params are NOT pre-colored; global proxies are used via MOV.
    // ==================================================================

    test_precolorize("Params get MOV from global proxy", R"(
fn add(a: i32, b: i32) -> i32 {
    'entry: {
        %0: i32 = @a + @b;
        return %0;
    }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = 1;
        return %0;
    }
}
)",
                     [](Program& prog, Precolorize& pass) {
                         auto colors = build_color_map(pass.precolored);
                         check(pass.precolored.proxies.size() == 64, "64 proxies");
                         auto& func = prog.findFunc("add");
                         // Params are NOT pre-colored
                         for (auto& p : func.params) {
                             check(colors.count(LeftValue{p->value()}) == 0,
                                   fmt::format("@{} is NOT directly pre-colored", p->name));
                         }
                         // Global proxies exist
                         auto* pa0 = find_global(prog, "__reg_a0");
                         auto* pa1 = find_global(prog, "__reg_a1");
                         check(pa0 != nullptr, "__reg_a0 global proxy exists");
                         check(pa1 != nullptr, "__reg_a1 global proxy exists");
                         check(colors.at(LeftValue{pa0->value()}) == 10,
                               "__reg_a0 -> register 10 (a0)");
                         check(colors.at(LeftValue{pa1->value()}) == 11,
                               "__reg_a1 -> register 11 (a1)");

                         auto* entry = func.entrance();
                         check(count_movs(*entry) >= 2,
                               ">=2 MOVs at entrance (proxy -> param)");
                     });

    test_precolorize("Mixed int+float params -- GPR & FPR globals", R"(
fn mixed(a: i32, x: f32, b: i32, y: f32) -> i32 {
    'entry: {
        %0: i32 = @a + @b;
        return %0;
    }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = 1;
        return %0;
    }
}
)",
                     [](Program& prog, Precolorize& pass) {
                         check(pass.precolored.proxies.size() == 64, "64 proxies");
                         // GPR global proxies
                         check(find_global(prog, "__reg_a0") != nullptr, "__reg_a0 exists");
                         check(find_global(prog, "__reg_a1") != nullptr, "__reg_a1 exists");
                         // FPR global proxies
                         check(find_global(prog, "__reg_f10") != nullptr, "__reg_f10 exists");
                         check(find_global(prog, "__reg_f11") != nullptr, "__reg_f11 exists");
                     });

    test_precolorize("9 params -- global proxies for a0-a7", R"(
fn many(a: i32, b: i32, c: i32, d: i32, e: i32, f: i32, g: i32, h: i32, i: i32) -> i32 {
    'entry: {
        %0: i32 = @a + @b;
        return %0;
    }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = 1;
        return %0;
    }
}
)",
                     [](Program& prog, Precolorize& pass) {
                         auto colors = build_color_map(pass.precolored);
                         check(pass.precolored.proxies.size() == 64, "64 proxies");
                         for (ssize_t r = 10; r <= 17; r++) {
                             auto name = fmt::format("__reg_a{}", r - 10);
                             auto* proxy = find_global(prog, name);
                             check(proxy != nullptr,
                                   fmt::format("{} exists (register {})", name, r));
                             if (proxy)
                                 check(colors.at(LeftValue{proxy->value()}) == r,
                                       fmt::format("{} -> register {}", name, r));
                         }
                     });

    // ==================================================================
    // Special register names.
    // ==================================================================

    test_precolorize("Special registers: zero, ra, sp, gp, tp", R"(
fn f() -> i32 { 'entry: { return 0; } }
)",
                     [](Program& prog, Precolorize& pass) {
                         auto colors = build_color_map(pass.precolored);
                         check(pass.precolored.proxies.size() == 64, "64 proxies");
                         auto check_name = [&](const std::string& name, ssize_t reg) {
                             auto* alloc = find_global(prog, name);
                             check(alloc != nullptr, fmt::format("{} exists", name));
                             if (alloc)
                                 check(colors.at(LeftValue{alloc->value()}) == reg,
                                       fmt::format("{} -> register {}", name, reg));
                         };
                         check_name("__reg_zero", 0);
                         check_name("__reg_ra", 1);
                         check_name("__reg_sp", 2);
                         check_name("__reg_gp", 3);
                         check_name("__reg_tp", 4);
                     });

    test_precolorize("GPR parameter registers a0-a7", R"(
fn f() -> i32 { 'entry: { return 0; } }
)",
                     [](Program& prog, Precolorize& pass) {
                         auto colors = build_color_map(pass.precolored);
                         for (ssize_t i = 0; i < 8; i++) {
                             auto name = fmt::format("__reg_a{}", i);
                             auto* alloc = find_global(prog, name);
                             check(alloc != nullptr, fmt::format("{} exists", name));
                             if (alloc)
                                 check(colors.at(LeftValue{alloc->value()}) == 10 + i,
                                       fmt::format("{} -> register {}", name, 10 + i));
                         }
                     });

    test_precolorize("Callee/caller-saved register names", R"(
fn f() -> i32 { 'entry: { return 0; } }
)",
                     [](Program& prog, Precolorize& pass) {
                         auto colors = build_color_map(pass.precolored);
                         auto check_name = [&](const std::string& name, ssize_t reg) {
                             auto* alloc = find_global(prog, name);
                             check(alloc != nullptr, fmt::format("{} exists", name));
                             if (alloc)
                                 check(colors.at(LeftValue{alloc->value()}) == reg,
                                       fmt::format("{} -> register {}", name, reg));
                         };
                         // Callee-saved
                         check_name("__reg_s0", 8);
                         check_name("__reg_s1", 9);
                         check_name("__reg_s2", 18);
                         check_name("__reg_s11", 27);
                         // Caller-saved
                         check_name("__reg_t0", 5);
                         check_name("__reg_t6", 31);
                     });

    test_precolorize("FPR registers f0-f31", R"(
fn f() -> i32 { 'entry: { return 0; } }
)",
                     [](Program& prog, Precolorize& pass) {
                         auto colors = build_color_map(pass.precolored);
                         for (size_t idx = 0; idx < 32; idx++) {
                             auto name = fmt::format("__reg_f{}", idx);
                             auto* alloc = find_global(prog, name);
                             check(alloc != nullptr, fmt::format("{} exists", name));
                             if (alloc)
                                 check(colors.at(LeftValue{alloc->value()})
                                           == static_cast<ssize_t>(idx),
                                       fmt::format("{} -> register {}", name, idx));
                         }
                     });

    // ==================================================================
    // Edge: empty program (no functions) still has 64 globals.
    // ==================================================================

    test_precolorize("Empty program still has 64 globals", R"(
)",
                     [](Program& prog, Precolorize& pass) {
                         check(pass.precolored.proxies.size() == 64, "64 proxies even with 0 functions");
                         check(prog.globals().size() == 64,
                               "64 global allocs even with 0 functions");
                         verify_all_64(prog, pass.precolored);
                     });

    fmt::println("\nResults: {} passed, {} failed", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
