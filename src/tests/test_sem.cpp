#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/syntax/visit.hpp"

void test_const_to_non_const() {
    auto text = R"(
double foo(double x[2], double y[2]) {
    return x[0] + y[0];
}

int main() {
    const double a[2][2] = { {1.0, 2.0}, {4.5e-2} };
    foo(a[0], a[1]);
    return 0;
}
)";
    auto stream = std::istringstream(text);
    try {
        auto code = ast::analysis(ast::parse(stream));
    } catch (const SemanticError& e) {
        fmt::println("Caught expected semantic error: {}", e.what());
        return;
    }
    assert(false && "Expected a semantic error due to incompatible pointer passing");
}

int main() {
    test_const_to_non_const();
    return 0;
}