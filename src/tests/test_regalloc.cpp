#include "backend/ir/lowering/abi.hpp"
#include "backend/ir/lowering/regalloc/main.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/rv64/abi.hpp"

#include <sstream>

using namespace ir::lowering;
using namespace ir::transform;

void test(const char* code, const TargetABI& abi) {
    auto stream = std::istringstream(code);
    auto prog_box = ir::parse(stream);
    auto& prog = *prog_box;
    NonSSAPassContext ctx(prog);
    RegisterAllocation regalloc(abi, true);
    regalloc.apply(prog, ctx);
    ctx.ud.verify();
    fmt::println("Final program:\n{}", prog);
    fmt::println("colored: {}", regalloc.colored);
    RedundantMoveElimination<NonSSAPassContext>(regalloc.colored, regalloc.precolored)
        .apply(prog, ctx);
    ctx.ud.verify();
    fmt::println("After redundant move elimination:\n{}", prog);
    fmt::println("---------------");
}

int main() {
    auto code = R"(
fn f(a: i32, b: i32) -> i32 {
    let mut d: i32;
    let mut e: i32;
    'entry: {
        @d: i32 = 0;
        @e: i32 = @a;
        => 'cond;
    }
    'cond: {
        %0: bool = @e > 0;
        => if %0 { 'then } else { 'exit };
    }
    'then: {
        @d: i32 = @d + @b;
        @e: i32 = @e - 1;
        => 'cond;
    }
    'exit: {
        return @d;
    }
}
)";
    RegisterABI regs = {.size = 3,
                        .caller_saved = {0, 1},
                        .callee_saved = {2},
                        .reserved = {},
                        .parameters = {0, 1},
                        .return_value = 0};
    RegisterABI ints = regs, floats = regs;
    ints.name = [](size_t index) { return fmt::format("r{}", index); };
    floats.name = [](size_t index) { return fmt::format("f{}", index); };
    TargetABI simple_target = {.reg = {.generals = ints, .floats = floats, .return_addr = 2},
                        .mem = rv64::ABI.mem};
    
    test(code, simple_target);

    auto code2 = R"(
fn main() -> i32 {
    let mut a_0: i32;
    let mut b_0: i32;
    'entry: {
        @a_0: i32 = 0;
        @a_0: i32 = 0;
        @b_0: i32 = 0;
        @b_0: i32 = 3;
        %0: bool = @a_0 == 5;
        => if %0 { 'if_true_6_2 } else { 'if_false_6_2 };
    }
    'if_true_6_2: {
        => 'while_cond_7_4;
    }
    'if_exit_6_2: {
        return @b_0;
    }
    'if_false_6_2: {
        => 'while_cond_13_4;
    }
    'while_cond_7_4: {
        %1: bool = @b_0 == 2;
        => if %1 { 'while_body_7_4 } else { 'while_exit_7_4 };
    }
    'while_body_7_4: {
        %2: i32 = @b_0 + 2;
        @b_0: i32 = %2;
        => 'while_cond_7_4;
    }
    'while_exit_7_4: {
        %3: i32 = @b_0 + 25;
        @b_0: i32 = %3;
        => 'if_exit_6_2;
    }
    'while_cond_13_4: {
        %4: bool = @a_0 < 5;
        => if %4 { 'while_body_13_4 } else { 'while_exit_13_4 };
    }
    'while_body_13_4: {
        %5: i32 = @b_0 * 2;
        @b_0: i32 = %5;
        %6: i32 = @a_0 + 1;
        @a_0: i32 = %6;
        => 'while_cond_13_4;
    }
    'while_exit_13_4: {
        => 'if_exit_6_2;
    }
}
    )";
    test(code2, simple_target);
    return 0;
}