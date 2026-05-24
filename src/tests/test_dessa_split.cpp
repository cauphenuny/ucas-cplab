#include "backend/ir/analysis/cfg.hpp"
#include "backend/ir/ir.h"
#include "backend/ir/optim/framework.hpp"
#include "backend/ir/optim/ssa.hpp"
#include "backend/ir/optim/ssa_destruct.hpp"
#include "backend/ir/parse/visit.hpp"
#include "fmt/base.h"

#include <functional>
#include <sstream>
#include <string>
#include <variant>

using namespace ir;
using namespace ir::analysis;
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

/// Mirrors the detection logic in SplitCriticalEdge::split:
/// counts edges (pred->blk) where blk has phi instructions, blk has >1 pred,
/// and pred has >1 succ.
int count_critical_edges(Func& func) {
    auto cfg = ControlFlowGraph(func);
    int count = 0;
    for (auto& blk : func.blocks()) {
        auto& preds = cfg.pred[blk.get()];
        if (preds.size() <= 1) continue;
        if (blk->insts().empty() || !std::holds_alternative<PhiInst>(blk->insts().front()))
            continue;
        for (auto pred : preds) {
            if (cfg.succ[pred].size() > 1) {
                ++count;
            }
        }
    }
    return count;
}

void test(const std::string& name, const std::string& text, bool build_ssa,
          const std::function<void(Func&, bool changed, size_t pre_count)>& verify = nullptr) {
    fmt::println("Test: {}", name);
    auto ir_stream = std::istringstream(text);
    try {
        auto program_box = ir::parse(ir_stream);
        auto& program = *program_box;

        if (build_ssa) {
            minipass::AddPhi{}.apply(program);
            minipass::Rename{}.apply(program);
        }
        fmt::println("Before SplitCriticalEdge:\n{}", program);

        auto& func = *program.funcs().front();
        auto pre_count = func.blocks().size();

        minipass::SplitCriticalEdge split;
        NonSSAPassContext ctx(program);
        bool changed = split.apply(program, ctx);

        fmt::println("After SplitCriticalEdge:\n{}", program);

        if (verify) {
            verify(func, changed, pre_count);
        }
    } catch (const std::exception& e) {
        fmt::println("  Error during test '{}': {}", name, e.what());
        ++tests_failed;
    }
    fmt::println("------------------------------------------");
}

int main() {
    // Test 1: Simple diamond -- no critical edges
    //
    //   entry
    //  /     \
    // then   else   (each has 1 successor -> NOT critical edges)
    //  \     /
    //   merge (phi for @x)
    //
    // Expected: no blocks inserted, changed == false
    test("Simple diamond (no critical edges)", R"(
fn main() -> i32 {
    let mut x: i32;
    'entry: {
        %0: bool = true;
        => if %0 { 'then_blk } else { 'else_blk };
    }
    'then_blk: {
        @x: i32 = 1;
        => 'merge;
    }
    'else_blk: {
        @x: i32 = 2;
        => 'merge;
    }
    'merge: {
        return @x;
    }
}
)",
         true, [&](Func& func, bool changed, size_t pre_count) {
             check(!changed, "SplitCriticalEdge returns false (no critical edges)");
             check(count_critical_edges(func) == 0, "no critical edges after pass");
             check(func.blocks().size() == pre_count, "block count unchanged");
         });

    // Test 2: One critical edge
    //
    //   entry ---------> merge   <- entry has 2 succs: merge and side
    //     \               ^
    //      side ----------+        (side has 1 succ)
    //
    // merge has 2 preds and phi (for @x) -> edge entry->merge is critical.
    // Expected: 1 intermediate block inserted; phi source updated.
    test("One critical edge", R"(
fn main() -> i32 {
    let mut x: i32;
    'entry: {
        %0: bool = true;
        @x: i32 = 1;
        => if %0 { 'merge } else { 'side };
    }
    'side: {
        @x: i32 = 2;
        => 'merge;
    }
    'merge: {
        return @x;
    }
}
)",
         true, [&](Func& func, bool changed, size_t pre_count) {
             check(changed, "SplitCriticalEdge returns true");
             check(count_critical_edges(func) == 0, "no critical edges after pass");
             check(func.blocks().size() == pre_count + 1, "one intermediate block inserted");

             // phi in 'merge' must no longer reference 'entry' directly
             auto merge_blk = func.findBlock("merge");
             auto entry_blk = func.findBlock("entry");
             if (merge_blk && !merge_blk->insts().empty() &&
                 std::holds_alternative<PhiInst>(merge_blk->insts().front())) {
                 auto& phi = std::get<PhiInst>(merge_blk->insts().front());
                 check(!phi.contains(entry_blk),
                       "phi in 'merge' no longer sources from 'entry' (redirected via mid)");
             }
         });

    // Test 3: Loop with three critical edges
    //
    //   entry -> loop_header ----> loop_exit
    //                 ^     \          ^
    //                 +--- body -------+
    //
    // loop_header has 2 succs (body, loop_exit) and phi (for @sum, @i from back-edge)
    // body        has 2 succs (loop_exit, loop_header)
    // loop_exit   has 2 preds (loop_header, body) and phi (for @sum)
    //
    // Critical edges:
    //   loop_header -> loop_exit  (loop_header: 2 succs; loop_exit: 2 preds + phi)
    //   body -> loop_exit         (body: 2 succs; loop_exit: 2 preds + phi)
    //   body -> loop_header       (body: 2 succs; loop_header: 2 preds + phi)
    //
    // Expected: 3 intermediate blocks inserted.
    test("Loop with three critical edges", R"(
fn main() -> i32 {
    let mut sum: i32;
    let mut i: i32;
    'entry: {
        @sum: i32 = 0;
        @i: i32 = 0;
        => 'loop_header;
    }
    'loop_header: {
        %0: bool = @i < 10;
        => if %0 { 'body } else { 'loop_exit };
    }
    'body: {
        @sum: i32 = @sum + @i;
        @i: i32 = @i + 1;
        %1: bool = @i < 5;
        => if %1 { 'loop_exit } else { 'loop_header };
    }
    'loop_exit: {
        return @sum;
    }
}
)",
         true, [&](Func& func, bool changed, size_t pre_count) {
             check(changed, "SplitCriticalEdge returns true");
             check(count_critical_edges(func) == 0, "no critical edges after pass");
             check(func.blocks().size() == pre_count + 3, "three intermediate blocks inserted");

             // phi sources must be updated to reference intermediate blocks, not originals
             auto loop_exit = func.findBlock("loop_exit");
             auto loop_header = func.findBlock("loop_header");
             auto body = func.findBlock("body");
             if (loop_exit && !loop_exit->insts().empty() &&
                 std::holds_alternative<PhiInst>(loop_exit->insts().front())) {
                 auto& phi = std::get<PhiInst>(loop_exit->insts().front());
                 check(!phi.contains(loop_header),
                       "phi in 'loop_exit' no longer sources from 'loop_header'");
                 check(!phi.contains(body), "phi in 'loop_exit' no longer sources from 'body'");
             }
         });

    // Test 4: Potential critical edge but no phi instructions (SSA not applied)
    //
    //   cond ---------> merge
    //     \               ^
    //      side ----------+
    //
    // Structurally cond->merge is a critical edge, but 'merge' has no phi
    // instructions, so SplitCriticalEdge must skip it.
    // Expected: no blocks inserted, changed == false.
    test("No phi instructions (no split expected)", R"(
fn main() -> i32 {
    'entry: {
        => 'cond;
    }
    'cond: {
        %0: bool = true;
        => if %0 { 'merge } else { 'side };
    }
    'side: {
        => 'merge;
    }
    'merge: {
        return 0;
    }
}
)",
         false, [&](Func& func, bool changed, size_t pre_count) {
             check(!changed, "SplitCriticalEdge returns false (no phi instructions)");
             check(func.blocks().size() == pre_count, "block count unchanged");
         });

    fmt::println("\nResults: {} passed, {} failed", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
