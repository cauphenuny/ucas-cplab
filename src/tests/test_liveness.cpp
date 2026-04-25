#include "backend/ir/analysis/cfg.hpp"
#include "backend/ir/analysis/dataflow/framework.hpp"
#include "backend/ir/analysis/dataflow/liveness.hpp"
#include "backend/ir/ir.h"
#include "backend/ir/parse/visit.hpp"
#include "fmt/base.h"

#include <functional>
#include <sstream>
#include <string>

using namespace ir::analysis;
using Liveness = flow::Liveness;

void test(const std::string& name, const std::string& text,
          const std::function<void(const ir::Program&, const ir::Func&, const DataFlow<Liveness>&)>&
              verify) {
    fmt::println("Test: {}", name);
    auto ir_stream = std::istringstream(text);
    try {
        auto program = ir::parse(ir_stream);
        for (auto& func_ptr : program.funcs()) {
            auto& func = *func_ptr;
            fmt::println("  Function: {}", func.name);
            auto cfg = ControlFlowGraph(func);
            auto live = DataFlow<Liveness>(cfg, program);
            fmt::println("{}", live.toString());
            if (verify) {
                verify(program, func, live);
            }
        }
    } catch (const std::exception& e) {
        fmt::println("  Error during test '{}': {}", name, e.what());
        exit(1);
    }
    fmt::println("------------------------------------------");
}

int main() {
    auto check = [](bool cond, const std::string& msg) {
        if (cond) {
            fmt::println("    [OK] {}", msg);
        } else {
            fmt::println("    [FAIL] {}", msg);
        }
    };

    // 1. Basic Sequence - check if params and deps are correctly handled
    test("Basic Sequence",
         R"(
fn add(a: i32, b: i32) -> i32 {
    'entry: {
        %0: i32 = @a;
        %1: i32 = @b;
        %2: i32 = %0 + %1;
        return %2;
    }
}

fn main() -> i32 {
    'entry: {
        %0: i32 = @add(1, 2);
        return %0;
    }
}
)",
         [&](const ir::Program& prog, const ir::Func& func, const DataFlow<Liveness>& live) {
             auto entry = func.findBlock("entry");
             if (func.name == "add") {
                 auto a = func.findAlloc("a")->value();
                 auto b = func.findAlloc("b")->value();
                 check(live.in.at(entry).contains(a), "add: a is live-in at entry");
                 check(live.in.at(entry).contains(b), "add: b is live-in at entry");
             } else if (func.name == "main") {
                 check(live.in.at(entry).size() == 0, "main: entry in-set is empty");
             }
         });

    // 2. Branching - check merging of liveness
    test("Branching",
         R"(
fn branch_test(cond: bool, a: i32, b: i32) -> i32 {
    let mut res: i32;
    'entry: {
        => if @cond { 'then_blk } else { 'else_blk };
    }
    'then_blk: {
        %0: i32 = @a;
        %1: i32 = %0 + 10;
        @res: i32 = %1;
        => 'exit_blk;
    }
    'else_blk: {
        %2: i32 = @b;
        %3: i32 = %2 + 20;
        @res: i32 = %3;
        => 'exit_blk;
    }
    'exit_blk: {
        %4: i32 = @res;
        return %4;
    }
}

fn main() -> i32 {
    'entry: {
        %0: bool = true;
        %1: i32 = @branch_test(%0, 1, 2);
        return %1;
    }
}
)",
         [&](const ir::Program& prog, const ir::Func& func, const DataFlow<Liveness>& live) {
             if (func.name == "branch_test") {
                 auto entry = func.findBlock("entry");
                 auto then_blk = func.findBlock("then_blk");
                 auto else_blk = func.findBlock("else_blk");
                 auto cond = func.findAlloc("cond")->value();
                 auto a = func.findAlloc("a")->value();
                 auto b = func.findAlloc("b")->value();
                 auto res = func.findAlloc("res")->value();

                 check(live.in.at(entry).contains(cond), "branch_test: cond is live-in at entry");
                 check(live.in.at(entry).contains(a), "branch_test: a is live-in at entry");
                 check(live.in.at(entry).contains(b), "branch_test: b is live-in at entry");
                 check(live.out.at(then_blk).contains(res),
                       "branch_test: res is live-out at then_blk");
                 check(live.out.at(else_blk).contains(res),
                       "branch_test: res is live-out at else_blk");
             }
         });

    // 3. Loops - check loop-carried dependencies
    test("Loops",
         R"(
fn loop_test(n: i32) -> i32 {
    let mut i: i32;
    let mut s: i32;
    'entry: {
        @i: i32 = 0;
        @s: i32 = 0;
        => 'loop_cond;
    }
    'loop_cond: {
        %0: i32 = @i;
        %1: bool = %0 < @n;
        => if %1 { 'loop_body } else { 'loop_exit };
    }
    'loop_body: {
        %2: i32 = @s;
        %3: i32 = @i;
        %4: i32 = %2 + %3;
        @s: i32 = %4;
        %5: i32 = @i;
        %6: i32 = %5 + 1;
        @i: i32 = %6;
        => 'loop_cond;
    }
    'loop_exit: {
        %7: i32 = @s;
        return %7;
    }
}
)",
         [&](const ir::Program& prog, const ir::Func& func, const DataFlow<Liveness>& live) {
             if (func.name == "loop_test") {
                 auto loop_cond = func.findBlock("loop_cond");
                 auto loop_body = func.findBlock("loop_body");
                 auto i = func.findAlloc("i")->value();
                 auto s = func.findAlloc("s")->value();
                 auto n = func.findAlloc("n")->value();

                 check(live.in.at(loop_cond).contains(i), "loop_test: i is live-in at loop_cond");
                 check(live.in.at(loop_cond).contains(s), "loop_test: s is live-in at loop_cond");
                 check(live.in.at(loop_cond).contains(n), "loop_test: n is live-in at loop_cond");

                 check(live.out.at(loop_body).contains(i), "loop_test: i is live-out at loop_body");
                 check(live.out.at(loop_body).contains(s), "loop_test: s is live-out at loop_body");
             }
         });

    // 4. Global Variables - check if globals are live-out at ReturnExit
    test("Global Variables",
         R"(
let ref mut g1: i32 = 0;
let ref mut g2: i32 = 0;

fn main() -> i32 {
    'entry: {
        %0: i32 = * @g1;
        return %0;
    }
}
)",
         [&](const ir::Program& prog, const ir::Func& func, const DataFlow<Liveness>& live) {
             if (func.name == "main") {
                 auto entry = func.findBlock("entry");
                 auto g1 = prog.findAlloc("g1")->value();
                 auto g2 = prog.findAlloc("g2")->value();

                 check(live.in.at(entry).contains(g1), "global: g1 is live-in at entry");
                 check(live.in.at(entry).contains(g2), "global: g2 is live-in at entry");
                 check(live.out.at(entry).contains(g1), "global: g1 is live-out at entry");
                 check(live.out.at(entry).contains(g2), "global: g2 is live-out at entry");
             }
         });

    return 0;
}
