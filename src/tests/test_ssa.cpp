#include "backend/ir/optim/ssa.hpp"
#include "backend/ir/parse/visit.hpp"
#include "fmt/base.h"

#include <sstream>

void test(const std::string& name, const std::string& text) {
    fmt::println("Test: {}", name);
    auto ir_stream = std::istringstream(text);
    try {
        auto program = ir::parse(ir_stream);
        fmt::println("Before AddPhi:\n{}", program);

        ir::optim::minipass::AddPhi add_phi;
        add_phi.apply(program);

        fmt::println("After AddPhi:\n{}", program);

        ir::optim::minipass::Rename rename;
        rename.apply(program);
        fmt::println("After Rename:\n{}", program);
    } catch (const std::exception& e) {
        fmt::println("  Error during test '{}': {}", name, e.what());
    }
    fmt::println("------------------------------------------");
}

int main() {
    test("Simple Diamond", R"(
fn main() -> i32 {
    let mut x: i32;
    'entry: {
        $0: bool = true;
        branch $0 ? 'then_blk : 'else_blk;
    }
    'then_blk: {
        x: i32 = 1;
        jump 'exit_blk;
    }
    'else_blk: {
        x: i32 = 2;
        jump 'exit_blk;
    }
    'exit_blk: {
        return x;
    }
}
)");

    test("Loop with redefined variable", R"(
fn main() -> i32 {
    let mut i: i32;
    let mut sum: i32;
    'entry: {
        i: i32 = 0;
        sum: i32 = 0;
        jump 'cond;
    }
    'cond: {
        $0: bool = i < 10;
        branch $0 ? 'body : 'exit;
    }
    'body: {
        sum: i32 = sum + i;
        i: i32 = i + 1;
        jump 'cond;
    }
    'exit: {
        return sum;
    }
}
)");

    return 0;
}
