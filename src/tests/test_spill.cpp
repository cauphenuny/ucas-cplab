/// Test Spill pass: convert NamedValue and TempValue to memory references,
/// inserting LOAD/STORE instructions so that spilled values are accessed
/// through memory-backed allocs.

#include "backend/ir/analysis/utils.hpp"
#include "backend/ir/ir.h"
#include "backend/ir/lowering/regalloc/spill.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/ir/vm/vm.h"
#include "fmt/base.h"

#include <functional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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

/// Get the NamedValue for a function's alloc by name.
LeftValue named(const Func& func, const std::string& name) {
    return LeftValue{func.findAlloc(name)->value()};
}

/// Construct a TempValue LeftValue by id (equality uses id + func* only, type ignored).
LeftValue temp(const Func& func, size_t id) {
    return LeftValue{TempValue{ir::type::construct<int>(), id, const_cast<Func*>(&func)}};
}

/// Count LOAD/Unary and STORE/Binary instructions in a block.
struct InstCounts {
    size_t loads = 0;
    size_t stores = 0;
    size_t total = 0;
};

InstCounts count_io(const Block& block) {
    InstCounts c;
    for (const auto& inst : block.insts()) {
        c.total++;
        if (auto* unary = std::get_if<UnaryInst>(&inst)) {
            if (unary->op == UnaryInstOp::LOAD) c.loads++;
        } else if (auto* binary = std::get_if<BinaryInst>(&inst)) {
            if (binary->op == InstOp::STORE) c.stores++;
        }
    }
    return c;
}

/// Parse IR, apply Spill with values produced by a factory, and pass to a
/// verification callback.
using ValuesFactory = std::function<std::vector<LeftValue>(Program&)>;

void test_spill(const std::string& name, const std::string& ir_text, ValuesFactory make_values,
                const std::function<void(Program&, bool changed)>& verify) {
    fmt::println("Test: {}", name);
    try {
        auto stream = std::istringstream(ir_text);
        auto prog_box = ir::parse(stream);
        auto& prog = *prog_box;
        fmt::println("Before:\n{}", prog);

        NonSSAPassContext ctx(prog);
        auto values = make_values(prog);
        bool changed = Spill(std::move(values)).apply(prog, ctx);
        fmt::println("After (changed={}):\n{}", changed, prog);

        verify(prog, changed);
    } catch (const std::exception& e) {
        fmt::println("  Error: {}", e.what());
        ++tests_failed;
    }
    fmt::println("------------------------------------------");
}

/// Parse IR, apply Spill, execute via VM, check return value.
void test_spill_and_run(const std::string& name, const std::string& ir_text,
                        ValuesFactory make_values, int expected) {
    fmt::println("Test: {}", name);
    try {
        auto stream = std::istringstream(ir_text);
        auto prog_box = ir::parse(stream);
        auto& prog = *prog_box;
        fmt::println("Before:\n{}", prog);

        NonSSAPassContext ctx(prog);
        auto values = make_values(prog);
        bool changed = Spill(std::move(values)).apply(prog, ctx);
        fmt::println("After (changed={}):\n{}", changed, prog);

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

/// Expect an exception when spilling.
void test_spill_throws(const std::string& name, const std::string& ir_text,
                       ValuesFactory make_values) {
    fmt::println("Test: {}", name);
    bool threw = false;
    try {
        auto stream = std::istringstream(ir_text);
        auto prog_box = ir::parse(stream);
        auto& prog = *prog_box;
        NonSSAPassContext ctx(prog);
        auto values = make_values(prog);
        Spill(std::move(values)).apply(prog, ctx);
    } catch (const std::exception& e) {
        fmt::println("  Caught expected exception: {}", e.what());
        threw = true;
    }
    check(threw, "Spill SSAValue throws an exception");
    fmt::println("------------------------------------------");
}

int main() {
    // ------------------------------------------------------------------
    // Test 1: Empty spill list.
    // Spill({}) should return false and leave the IR unchanged.
    // ------------------------------------------------------------------
    test_spill(
        "Empty spill list", R"(
fn main() -> i32 {
    'entry: {
        %0: i32 = 42;
        return %0;
    }
}
)",
        [](Program&) { return std::vector<LeftValue>{}; },
        [](Program& prog, bool changed) {
            check(!changed, "apply returns false (empty spill list)");
            auto& func = prog.findFunc("main");
            auto* entry = func.findBlock("entry");
            auto counts = count_io(*entry);
            check(counts.loads == 0, "no LOAD instructions");
            check(counts.stores == 0, "no STORE instructions");
        });

    // ------------------------------------------------------------------
    // Test 2: Spill a NamedValue (local variable).
    //   @x: i32 = 42; return @x;
    // After spill: @x is reference, LOAD/STORE inserted.
    // ------------------------------------------------------------------
    test_spill(
        "Spill NamedValue (local)", R"(
fn main() -> i32 {
    let mut x: i32;
    'entry: {
        @x: i32 = 42;
        return @x;
    }
}
)",
        [](Program& prog) {
            auto& func = prog.findFunc("main");
            return std::vector{named(func, "x")};
        },
        [](Program& prog, bool changed) {
            check(changed, "apply returns true");
            auto& func = prog.findFunc("main");
            auto* xalloc = func.findAlloc("x");
            check(xalloc->reference, "@x alloc is reference after spill");
            auto* entry = func.findBlock("entry");
            auto counts = count_io(*entry);
            check(counts.loads >= 1, "at least 1 LOAD in entry");
            check(counts.stores >= 1, "at least 1 STORE in entry");
        });

    // ------------------------------------------------------------------
    // Test 3: Spill a TempValue.
    //   %0: i32 = 42; return %0;
    // Spill creates __spill_0 alloc, then spills it.
    // ------------------------------------------------------------------
    test_spill(
        "Spill TempValue", R"(
fn main() -> i32 {
    'entry: {
        %0: i32 = 42;
        return %0;
    }
}
)",
        [](Program& prog) {
            auto& func = prog.findFunc("main");
            return std::vector{temp(func, 0)};
        },
        [](Program& prog, bool changed) {
            check(changed, "apply returns true");
            auto& func = prog.findFunc("main");
            auto* spill_alloc = func.findAlloc("__spill_0");
            check(spill_alloc != nullptr, "__spill_0 alloc was created");
            check(spill_alloc->reference, "__spill_0 alloc is reference after spill");
            auto* entry = func.findBlock("entry");
            auto counts = count_io(*entry);
            check(counts.loads >= 1, "at least 1 LOAD in entry");
            check(counts.stores >= 1, "at least 1 STORE in entry");
        });

    // ------------------------------------------------------------------
    // Test 4: Spill both NamedValue and TempValue together.
    //   @x: i32 = 10; %0: i32 = @x + 5; return %0;
    // ------------------------------------------------------------------
    test_spill(
        "Spill mixed NamedValue + TempValue", R"(
fn main() -> i32 {
    let mut x: i32;
    'entry: {
        @x: i32 = 10;
        %0: i32 = @x + 5;
        return %0;
    }
}
)",
        [](Program& prog) {
            auto& func = prog.findFunc("main");
            return std::vector{named(func, "x"), temp(func, 0)};
        },
        [](Program& prog, bool changed) {
            check(changed, "apply returns true");
            auto& func = prog.findFunc("main");
            auto* xalloc = func.findAlloc("x");
            check(xalloc->reference, "@x alloc is reference after spill");
            auto* spill_alloc = func.findAlloc("__spill_0");
            check(spill_alloc != nullptr, "__spill_0 alloc was created");
            check(spill_alloc->reference, "__spill_0 alloc is reference after spill");
            auto* entry = func.findBlock("entry");
            auto counts = count_io(*entry);
            check(counts.loads >= 2, "at least 2 LOADs in entry");
            check(counts.stores >= 2, "at least 2 STOREs in entry");
        });

    // ------------------------------------------------------------------
    // Test 5: Spill a function parameter (NamedValue).
    // ------------------------------------------------------------------
    test_spill(
        "Spill function param", R"(
fn main(n: i32) -> i32 {
    'entry: {
        return @n;
    }
}
)",
        [](Program& prog) {
            auto& func = prog.findFunc("main");
            return std::vector{named(func, "n")};
        },
        [](Program& prog, bool changed) {
            check(changed, "apply returns true");
            auto& func = prog.findFunc("main");
            auto* nalloc = func.findAlloc("n");
            check(nalloc->reference, "@n alloc is reference after spill");
            auto* entry = func.findBlock("entry");
            auto counts = count_io(*entry);
            check(counts.loads >= 1, "at least 1 LOAD in entry");
        });

    // ------------------------------------------------------------------
    // Test 6: Spill SSAValue throws UNIMPLEMENTED_ERROR.
    // ------------------------------------------------------------------
    test_spill_throws("Spill SSAValue throws", R"(
fn main() -> i32 {
    let mut x: i32;
    'entry: {
        @x: i32 = 1;
        return @x;
    }
}
)",
                      [](Program& prog) {
                          auto& func = prog.findFunc("main");
                          auto* x = func.findAlloc("x");
                          return std::vector{LeftValue{SSAValue{type::construct<int>(), x, 0}}};
                      });

    // ------------------------------------------------------------------
    // Test 7: Functional — spill NamedValue, execute via VM.
    //   @x = 10; @y = @x + @x; return @y;  -->  20
    // ------------------------------------------------------------------
    test_spill_and_run(
        "Spill NamedValue — VM execution", R"(
fn main() -> i32 {
    let mut x: i32;
    let mut y: i32;
    'entry: {
        @x: i32 = 10;
        %0: i32 = @x + @x;
        @y: i32 = %0;
        return @y;
    }
}
)",
        [](Program& prog) {
            auto& func = prog.findFunc("main");
            return std::vector{named(func, "x"), named(func, "y")};
        },
        20);

    // ------------------------------------------------------------------
    // Test 8: Functional — spill TempValue, execute via VM.
    //   %0 = 7; %1 = %0 * 3; return %1;  -->  21
    // ------------------------------------------------------------------
    test_spill_and_run(
        "Spill TempValue — VM execution", R"(
fn main() -> i32 {
    'entry: {
        %0: i32 = 7;
        %1: i32 = %0 * 3;
        return %1;
    }
}
)",
        [](Program& prog) {
            auto& func = prog.findFunc("main");
            return std::vector{temp(func, 0), temp(func, 1)};
        },
        21);

    // ------------------------------------------------------------------
    // Test 9: Functional — spill both NamedValue and TempValue, execute via VM.
    //   @x = 10; %0 = @x + 5; return %0;  -->  15
    // ------------------------------------------------------------------
    test_spill_and_run(
        "Spill mixed NamedValue + TempValue — VM execution", R"(
fn main() -> i32 {
    let mut x: i32;
    'entry: {
        @x: i32 = 10;
        %0: i32 = @x + 5;
        return %0;
    }
}
)",
        [](Program& prog) {
            auto& func = prog.findFunc("main");
            return std::vector{named(func, "x"), temp(func, 0)};
        },
        15);

    // ------------------------------------------------------------------
    // Test 10: Spill with loop — all blocks get LOAD/STORE.
    //   Sum 0..4 = 10 using @sum and @i.
    // ------------------------------------------------------------------
    test_spill(
        "Spill loop variables", R"(
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
        [](Program& prog) {
            auto& func = prog.findFunc("main");
            return std::vector{named(func, "sum"), named(func, "i")};
        },
        [](Program& prog, bool changed) {
            check(changed, "apply returns true");
            auto& func = prog.findFunc("main");
            check(func.findAlloc("sum")->reference, "@sum is reference");
            check(func.findAlloc("i")->reference, "@i is reference");
            for (auto* bname : {"entry", "cond", "body", "exit"}) {
                auto* block = func.findBlock(bname);
                auto counts = count_io(*block);
                check(counts.loads + counts.stores > 0,
                      fmt::format("block '{} has LOAD/STORE", bname));
            }
        });

    // ------------------------------------------------------------------
    // Test 11: Functional — loop with spilled variables, execute via VM.
    //   Sum 0..4 = 10 using @sum and @i.
    // ------------------------------------------------------------------
    test_spill_and_run(
        "Spill loop — VM execution", R"(
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
        [](Program& prog) {
            auto& func = prog.findFunc("main");
            return std::vector{named(func, "sum"), named(func, "i")};
        },
        10);

    // ------------------------------------------------------------------
    // Test 12: Spill a global variable (NamedValue from global Alloc).
    // ------------------------------------------------------------------
    test_spill(
        "Spill global variable", R"(
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
        [](Program& prog) {
            auto* x = prog.findAlloc("x");
            return std::vector{LeftValue{x->value()}};
        },
        [](Program& prog, bool changed) {
            check(changed, "apply returns true");
            auto* xalloc = prog.findAlloc("x");
            check(xalloc->reference, "global x is reference after spill");
        });

    // ------------------------------------------------------------------
    // Test 13: Spill with diamond control flow — all blocks get LOAD/STORE.
    //   x=11, y=2, return 13.
    // ------------------------------------------------------------------
    test_spill_and_run(
        "Spill diamond control flow — VM execution", R"(
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
        [](Program& prog) {
            auto& func = prog.findFunc("main");
            return std::vector{named(func, "x"), named(func, "y")};
        },
        13);

    fmt::println("\nResults: {} passed, {} failed", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
