#include "backend/ir/gen/irgen.h"
#include "backend/ir/parse/visit.hpp"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/syntax/visit.hpp"

int main() {
    {
        /// NOTE: test remap
        auto text = R"(
fn main() -> i32 {
.entry:
  $2: i32 = 1;
  $1: i32 = 2;
  $0: i32 = $2 + $1;
  return $0;
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
.entry:
  $0: i32 = 1;
  $1: i32 = 2;
  $0: i32 = $0 + $1;
  return $0;
}
)";
        auto ir_stream = std::istringstream(text);
        try {
            auto ir_code = ir::parse(ir_stream);
            exit(1); // expect to fail
        } catch (const std::exception& e) {
            fmt::println("Exception: {}", e.what());
        }
    }
    return 0;
}