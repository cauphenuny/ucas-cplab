#include "backend/ir/ir.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/ir/vm/vm.h"
#include "fmt/base.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>

int main() {
    {
        /// NOTE: test remap
        auto text = R"(
fn main() -> i32 {
    'entry: {
        $2: i32 = 1;
        $1: i32 = 2;
        $0: i32 = $2 + $1;
        return $0;
    }
}
)";
        auto ir_stream = std::istringstream(text);
        auto ir_code = ir::parse(ir_stream);
        fmt::println("Reconstructed IR: \n{}", ir_code);
    }
    {
        /// NOTE: test Single Assignment
        auto text = R"(
fn main() -> i32 {
    'entry: {
        $0: i32 = 1;
        $1: i32 = 2;
        $0: i32 = $0 + $1;
        return $0;
    }
}
)";
        auto ir_stream = std::istringstream(text);
        try {
            auto ir_code = ir::parse(ir_stream);
            exit(1);  // expect to fail
        } catch (const std::exception& e) {
            fmt::println("Exception: {}", e.what());
        }
    }
    {
        /// NOTE: test variable's Single Assignment
        auto text = R"(
fn main() -> i32 {
    let a: i32 = 0;
    'entry: {
        a: i32 = 1;
        $0: i32 = a;
        return $0;
    }
}
)";
        auto ir_stream = std::istringstream(text);
        try {
            auto ir_code = ir::parse(ir_stream);
            exit(1);  // expect to fail
        } catch (const std::exception& e) {
            fmt::println("Exception: {}", e.what());
        }
    }
    {
        /// NOTE: test variable's Single Assignment
        auto text = R"(
fn main() -> i32 {
    let a: i32;
    'entry: {
        a: i32 = 1;
        jump 'assign;
    }
    'assign: {
        a: i32 = 2;
        $0: i32 = a;
        return $0;
    }
}
)";
        auto ir_stream = std::istringstream(text);
        try {
            auto ir_code = ir::parse(ir_stream);
            exit(1);  // expect to fail
        } catch (const std::exception& e) {
            fmt::println("Exception: {}", e.what());
        }
    }
    {
        /// NOTE: test same name
        auto text = R"(
fn a_1_2() -> i32 {
    'entry: {
        $0: i32 = 1;
        return $0;
    }
}
fn main() -> i32 {
    let a_1_2: i32 = 3;
    'entry: {
        $0: i32 = a_1_2();
        return $0;
    }
}
)";
        auto ir_stream = std::istringstream(text);
        auto ir_code = ir::parse(ir_stream);
        fmt::println("Reconstructed IR: \n{}", ir_code);
        ir::vm::VirtualMachine env(std::cin, std::cout);
        auto ret = env.execute(ir_code);
        assert(ret == 1);
    }
    return 0;
}