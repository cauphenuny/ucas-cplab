/// Test PreColorize pass: pre-colored register assignments for call args,
/// return values, and function parameters.
/// Uses the RV64 ABI for register assignment.

#include "backend/ir/ir.h"
#include "backend/ir/lowering/regalloc/precolorize.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/rv64/abi.hpp"
#include "fmt/base.h"

#include <functional>
#include <set>
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

/// Collect all register numbers from a ColorMap into a set.
std::set<ssize_t> collect_regs(const ColorMap& map) {
    std::set<ssize_t> regs;
    for (const auto& [lv, reg] : map) regs.insert(reg);
    return regs;
}

/// Count how many entries in the ColorMap have the given register number.
size_t count_by_reg(const ColorMap& map, ssize_t reg) {
    size_t count = 0;
    for (const auto& [lv, r] : map)
        if (r == reg) count++;
    return count;
}

/// Count MOV instructions in a block.
size_t count_movs(const Block& block) {
    size_t count = 0;
    for (const auto& inst : block.insts()) {
        if (auto* unary = std::get_if<UnaryInst>(&inst)) {
            if (unary->op == UnaryInstOp::MOV) count++;
        }
    }
    return count;
}

/// Run PreColorize and pass the result to a verification callback.
void test_precolorize(const std::string& name, const std::string& ir_text,
                      const std::function<void(Program&, const ColorMap&)>& verify) {
    fmt::println("Test: {}", name);
    try {
        auto stream = std::istringstream(ir_text);
        auto prog_box = ir::parse(stream);
        auto& prog = *prog_box;
        fmt::println("Before PreColorize:\n{}", prog);

        PreColorize pass(rv64::ABI);
        NonSSAPassContext ctx(prog);
        pass.apply(prog, ctx);

        fmt::println("After PreColorize:\n{}", prog);
        fmt::println("Precolored map (size={}):", pass.precolored.size());

        verify(prog, pass.precolored);
    } catch (const std::exception& e) {
        fmt::println("  Error: {}", e.what());
        ++tests_failed;
    }
    fmt::println("------------------------------------------");
}

int main() {
    // ------------------------------------------------------------------
    // Test 1: Call with 2 int args. add has 2 arg proxies + return = 3.
    // main: 2 call args + 1 retval + return = 4. Total: 7.
    // ------------------------------------------------------------------
    test_precolorize("Call with 2 int args", R"(
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
                     [](Program& prog, const ColorMap& precolored) {
                         auto regs = collect_regs(precolored);
                         check(regs.count(10) > 0,
                               "register 10 is used (first GPR param / return)");
                         check(regs.count(11) > 0, "register 11 is used (second GPR param)");
                         check(precolored.size() == 7, "7 entries (3 add + 4 main)");
                         auto& main_func = prog.findFunc("main");
                         auto* entry = main_func.findBlock("entry");
                         check(count_movs(*entry) >= 2, "at least 2 MOVs inserted before call");
                     });

    // ------------------------------------------------------------------
    // Test 2: Call with 9 int args — 9th spills.
    // many: 8 arg proxies + return = 9. main: 8 call args + 1 retval + return = 10.
    // Total: 19.
    // ------------------------------------------------------------------
    test_precolorize("Call with 9 int args — 9th spills", R"(
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
                     [](Program& prog, const ColorMap& precolored) {
                         auto regs = collect_regs(precolored);
                         for (ssize_t r = 10; r <= 17; r++)
                             check(regs.count(r) > 0,
                                   fmt::format("register {} is used (GPR param)", r));
                         check(precolored.size() == 19, "19 entries (9 many + 10 main)");
                     });

    // ------------------------------------------------------------------
    // Test 3: Call with 2 float args.
    // fadd: 2 arg proxies + return = 3. main: 2 call args + 1 retval + return = 4.
    // Total: 7.
    // ------------------------------------------------------------------
    test_precolorize("Call with 2 float args", R"(
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
                     [](Program& prog, const ColorMap& precolored) {
                         auto regs = collect_regs(precolored);
                         check(regs.count(10) > 0, "f10 is used (first FPR param)");
                         check(regs.count(11) > 0, "f11 is used (second FPR param)");
                         check(precolored.size() == 7, "7 entries (3 fadd + 4 main)");
                     });

    // ------------------------------------------------------------------
    // Test 4: Mixed int+float call args.
    // mixed: 4 arg proxies + return = 5. main: 4 call args + 1 retval + return = 6.
    // Total: 11.
    // ------------------------------------------------------------------
    test_precolorize("Call with mixed int+float args", R"(
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
                     [](Program& prog, const ColorMap& precolored) {
                         check(precolored.size() == 11, "11 entries (5 mixed + 6 main)");
                         auto& main_func = prog.findFunc("main");
                         auto* entry = main_func.findBlock("entry");
                         check(count_movs(*entry) >= 4, "at least 4 MOVs inserted (one per arg)");
                     });

    // ------------------------------------------------------------------
    // Test 5: Return value pre-color for i32.
    // answer: 1 return. main: 1 return. Total: 2.
    // ------------------------------------------------------------------
    test_precolorize("Return value pre-color (i32)", R"(
fn answer() -> i32 {
    'entry: {
        return 42;
    }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = 1;
        return %0;
    }
}
)",
                     [](Program& prog, const ColorMap& precolored) {
                         check(precolored.size() == 2, "2 entries (1 answer + 1 main)");
                         // Both returns use x10 (GPR return register)
                         check(count_by_reg(precolored, 10) >= 2,
                               "at least 2 usages of register 10 (returns)");
                     });

    // ------------------------------------------------------------------
    // Test 6: Return value pre-color for f64.
    // pi: 1 FP return. main: 1 GPR return. Total: 2.
    // ------------------------------------------------------------------
    test_precolorize("Return value pre-color (f64)", R"(
fn pi() -> f64 {
    'entry: {
        return 3.14159;
    }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = 1;
        return %0;
    }
}
)",
                     [](Program& prog, const ColorMap& precolored) {
                         check(precolored.size() == 2, "2 entries (1 pi + 1 main)");
                     });

    // ------------------------------------------------------------------
    // Test 7: Function without ReturnExit.
    // no_ret: 0 entries (no params, no return, no callee-saved).
    // main: 1 return. Total: 1.
    // ------------------------------------------------------------------
    test_precolorize("No return value in function", R"(
fn no_ret() -> i32 {
    'entry: {
        => 'entry;
    }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = 1;
        return %0;
    }
}
)",
                     [](Program& prog, const ColorMap& precolored) {
                         check(precolored.size() == 1, "1 entry (0 no_ret + 1 main)");
                         auto& no_ret_func = prog.findFunc("no_ret");
                         auto* no_ret_entry = no_ret_func.entrance();
                         check(count_movs(*no_ret_entry) == 0,
                               "no_ret entrance has 0 MOVs (no params, no return)");
                     });

    // ------------------------------------------------------------------
    // Test 8: Multiple calls in one function.
    // square: 1 arg proxy + return = 2. main: 2 call args + 2 retvals + return = 5.
    // Total: 7.
    // ------------------------------------------------------------------
    test_precolorize("Multiple calls in one function", R"(
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
                     [](Program& prog, const ColorMap& precolored) {
                         check(precolored.size() == 7, "7 entries (2 square + 5 main)");
                         auto& main_func = prog.findFunc("main");
                         auto* entry = main_func.findBlock("entry");
                         check(count_movs(*entry) >= 2,
                               "at least 2 MOVs inserted (one per call arg)");
                     });

    // ------------------------------------------------------------------
    // Test 9: Bool args use GPR (same as Int).
    // logic: 2 arg proxies + return = 3. main: 2 call args + 1 retval + return = 4.
    // Total: 7.
    // ------------------------------------------------------------------
    test_precolorize("Call with bool args uses GPR", R"(
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
                     [](Program& prog, const ColorMap& precolored) {
                         auto regs = collect_regs(precolored);
                         check(regs.count(10) > 0, "register 10 used (first bool param)");
                         check(regs.count(11) > 0, "register 11 used (second bool param)");
                         check(precolored.size() == 7, "7 entries (3 logic + 4 main)");
                     });

    // ------------------------------------------------------------------
    // Test 10: Call args include both temps and constexprs.
    // add: 2 arg proxies + return = 3. main: 2 call args + 1 retval + return = 4.
    // Total: 7.
    // ------------------------------------------------------------------
    test_precolorize("Call with temp args", R"(
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
                     [](Program& prog, const ColorMap& precolored) {
                         check(precolored.size() == 7, "7 entries (3 add + 4 main)");
                         auto& main_func = prog.findFunc("main");
                         auto* entry = main_func.findBlock("entry");
                         bool found_call = false;
                         for (auto& inst : entry->insts()) {
                             if (auto* call = std::get_if<CallInst>(&inst)) {
                                 found_call = true;
                                 for (auto& arg : call->args) {
                                     auto* lv = std::get_if<LeftValue>(&arg);
                                     check(lv && std::holds_alternative<TempValue>(*lv),
                                           "call arg is a TempValue (MOV'd to pre-colored temp)");
                                 }
                             }
                         }
                         check(found_call, "found call instruction");
                     });

    // ------------------------------------------------------------------
    // Test 11: Params get proxy allocs (__arg_xN) that are pre-colored.
    // add: 2 arg proxies + return = 3. main: 1 return = 1. Total: 4.
    // ------------------------------------------------------------------
    test_precolorize("Simple int params — proxy pre-colored", R"(
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
                     [](Program& prog, const ColorMap& precolored) {
                         auto& func = prog.findFunc("add");
                         // Params themselves are NOT pre-colored
                         for (auto& p : func.params) {
                             check(precolored.count(LeftValue{p->value()}) == 0,
                                   fmt::format("@{} is NOT directly pre-colored", p->name));
                         }
                         auto* px10 = func.findAlloc("__arg_x10");
                         auto* px11 = func.findAlloc("__arg_x11");
                         check(px10 != nullptr, "__arg_x10 proxy exists");
                         check(px11 != nullptr, "__arg_x11 proxy exists");
                         check(precolored.at(LeftValue{px10->value()}) == 10, "__arg_x10 -> x10");
                         check(precolored.at(LeftValue{px11->value()}) == 11, "__arg_x11 -> x11");
                         auto* entry = func.entrance();
                         check(count_movs(*entry) >= 2,
                               "at least 2 MOVs at entrance (proxy → param)");
                         check(precolored.size() == 4, "4 entries (3 add + 1 main)");
                     });

    // ------------------------------------------------------------------
    // Test 12: Mixed int+float params get separate GPR/FPR proxy allocs.
    // mixed: 4 arg proxies + return = 5. main: 1 return = 1. Total: 6.
    // ------------------------------------------------------------------
    test_precolorize("Mixed int+float params — proxy pre-colored", R"(
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
                     [](Program& prog, const ColorMap& precolored) {
                         auto& func = prog.findFunc("mixed");
                         auto* px10 = func.findAlloc("__arg_x10");
                         auto* px11 = func.findAlloc("__arg_x11");
                         check(px10 != nullptr, "__arg_x10 exists (for @a)");
                         check(px11 != nullptr, "__arg_x11 exists (for @b)");
                         check(precolored.at(LeftValue{px10->value()}) == 10, "__arg_x10 -> x10");
                         check(precolored.at(LeftValue{px11->value()}) == 11, "__arg_x11 -> x11");
                         auto* pf10 = func.findAlloc("__arg_f10");
                         auto* pf11 = func.findAlloc("__arg_f11");
                         check(pf10 != nullptr, "__arg_f10 exists (for @x)");
                         check(pf11 != nullptr, "__arg_f11 exists (for @y)");
                         check(precolored.at(LeftValue{pf10->value()}) == 10, "__arg_f10 -> f10");
                         check(precolored.at(LeftValue{pf11->value()}) == 11, "__arg_f11 -> f11");
                         check(precolored.size() == 6, "6 entries (5 mixed + 1 main)");
                     });

    // ------------------------------------------------------------------
    // Test 13: 9 params — proxy only for first 8.
    // many: 8 arg proxies + return = 9. main: 1 return = 1. Total: 10.
    // ------------------------------------------------------------------
    test_precolorize("9 params — proxy only for first 8", R"(
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
                     [](Program& prog, const ColorMap& precolored) {
                         auto& func = prog.findFunc("many");
                         for (ssize_t r = 10; r <= 17; r++) {
                             auto name = fmt::format("__arg_x{}", r);
                             auto* proxy = func.findAlloc(name);
                             check(proxy != nullptr, fmt::format("{} exists", name));
                             check(precolored.at(LeftValue{proxy->value()}) == r,
                                   fmt::format("{} -> x{}", name, r));
                         }
                         bool found_x18 = true;
                         try {
                             (void)func.findAlloc("__arg_x18");
                         } catch (const std::exception&) {
                             found_x18 = false;
                         }
                         check(!found_x18, "no __arg_x18 (9th param spilled)");
                         check(precolored.size() == 10, "10 entries (9 many + 1 main)");
                     });

    // ------------------------------------------------------------------
    // Test 14: Function with no params.
    // no_params: 1 return. main: 1 return. Total: 2.
    // ------------------------------------------------------------------
    test_precolorize("No params — no proxy allocs", R"(
fn no_params() -> i32 {
    'entry: {
        return 42;
    }
}
fn main() -> i32 {
    'entry: {
        %0: i32 = 1;
        return %0;
    }
}
)",
                     [](Program& prog, const ColorMap& precolored) {
                         auto& func = prog.findFunc("no_params");
                         check(func.params.empty(), "function has no params");
                         check(precolored.size() == 2, "2 entries (1 no_params + 1 main)");
                     });

    fmt::println("\nResults: {} passed, {} failed", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
