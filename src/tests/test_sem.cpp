#include "backend/ir/gen/irgen.h"
#include "fmt/base.h"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/syntax/visit.hpp"
#include "utils/error.hpp"

#include <cassert>
#include <sstream>

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
        auto program = ir::gen::generate(code);
        fmt::println("Generated IR:\n{}", program);
    } catch (const SemanticError& e) {
        fmt::println("Caught expected semantic error: {}", e.what());
        return;
    }
    assert(false && "Expected a semantic error due to incompatible pointer passing");
}

void test_non_const_to_non_const() {
    auto text = R"(
double foo(double x[2], double y[2]) {
    return x[0] + y[0];
}

int main() {
    double a[2][2] = { {1.0, 2.0}, {4.5e-2} };
    double b[2][2] = { {1.0}, {4.5e-2, 2.0} };
    foo(a[0], a[1]);
    a[1][1] = 0.0;
    return 0;
}
)";
    auto stream = std::istringstream(text);
    try {
        auto code = ast::analysis(ast::parse(stream));
        auto program = ir::gen::generate(code);
        fmt::println("Generated IR:\n{}", program);
    } catch (const SemanticError& e) {
        fmt::println("Caught expected semantic error: {}", e.what());
        assert(
            false &&
            "Unexpected semantic error: non-const to non-const pointer passing should be allowed");
        return;
    }
}

void test_const_to_non_const_2() {
    auto text = R"(
int main() {
    const double b[2] = {1.0, 2.0};
    b[0] = 1.0;
    return 0;
}
)";
    auto stream = std::istringstream(text);
    try {
        auto code = ast::analysis(ast::parse(stream));
        auto program = ir::gen::generate(code);
        fmt::println("Generated IR:\n{}", program);
    } catch (const SemanticError& e) {
        fmt::println("Caught expected semantic error: {}", e.what());
        return;
    }
    assert(false && "Expected a semantic error due to incompatible pointer passing");
}

void test_const_to_non_const_3() {
    auto text = R"(
int main() {
    const double b = 1.0;
    b = 1.0;
    return 0;
}
)";
    auto stream = std::istringstream(text);
    try {
        auto code = ast::analysis(ast::parse(stream));
        auto program = ir::gen::generate(code);
        fmt::println("Generated IR:\n{}", program);
    } catch (const SemanticError& e) {
        fmt::println("Caught expected semantic error: {}", e.what());
        return;
    }
    assert(false && "Expected a semantic error due to incompatible pointer passing");
}

void test_multidim() {
    auto text = R"(
int b[2][2][2] = { {1,2,3,4} }; 
int main() {
    b[0][0][0] = 1;
    b[0][0][1] = 2;
    b[0][1][0] = 3;
    b[0][1][1] = 4;
    return 0;
}
)";
    auto stream = std::istringstream(text);
    try {
        auto code = ast::analysis(ast::parse(stream));
        auto program = ir::gen::generate(code);
        fmt::println("Generated IR:\n{}", program);
    } catch (const SemanticError& e) {
        fmt::println("Caught semantic error: {}", e.what());
        return;
    }
    assert(false && "Expected a semantic error due to incompatible array construction");
}

void test_multidim_2() {
    auto text = R"(
int a[4] = {1,2, {3,4}};
int main() {
    return 0;
}
)";
    auto stream = std::istringstream(text);
    try {
        auto code = ast::analysis(ast::parse(stream));
        auto program = ir::gen::generate(code);
        fmt::println("Generated IR:\n{}", program);
    } catch (const SemanticError& e) {
        fmt::println("Caught expected semantic error: {}", e.what());
        return;
    }
    assert(false && "Expected a semantic error due to incompatible array construction");
}

int main() {
    test_const_to_non_const();
    test_non_const_to_non_const();
    test_const_to_non_const_2();
    test_const_to_non_const_3();
    test_multidim();
    test_multidim_2();
    return 0;
}