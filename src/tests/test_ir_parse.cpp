#include "backend/ir/parse/visit.hpp"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/syntax/visit.hpp"
#include "backend/ir/gen/irgen.h"

int main() {
    auto ast = ast::analysis(ast::parse(std::cin));
    auto code = ir::gen::generate(ast);
    auto ir_text = fmt::format("{}\n", code);
    auto ir_stream = std::istringstream(ir_text);
    auto ir_code = ir::parse(ir_stream);
    fmt::println("{}", ir_code);
    return 0;
}