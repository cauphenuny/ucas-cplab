#include "backend/ir/ir.hpp"
#include "backend/ir/vm/vm.h"
#include "backend/ir/gen/irgen.h"
#include "frontend/syntax/visit.hpp"

#include <sstream>
#include <cassert>

void test_builtin() {
    auto text = R"(
int main() {
    print_int(123);
    return 21 + 20 + 1;
}
    )";
    fmt::println("Code: {}\n", text);
    auto stream = std::istringstream(text);
    auto code = ast::analysis(ast::parse(stream));
    auto prog = ir::gen::generate(code);
    fmt::println("Generated IR:\n{}\n", prog);
    auto out_stream = std::ostringstream{};
    auto env = ir::vm::VirtualMachine(std::cin, out_stream);
    int ret = env.execute(prog);
    fmt::println("Output: \n{}\n", out_stream.str());
    assert(ret == 42);
}

void test_index() {
    auto text = R"(
int main() {
    int arr[3] = {10, 20, 30};
    print_int(arr[1] - arr[2]);
    return arr[0];
})";
    auto stream = std::istringstream(text);
    fmt::println("Code: {}", text);
    auto code = ast::analysis(ast::parse(stream));
    auto prog = ir::gen::generate(code);
    fmt::println("Generated IR:\n{}\n", prog);
    auto out_stream = std::ostringstream{};
    auto env = ir::vm::VirtualMachine(std::cin, out_stream);
    int ret = env.execute(prog);
    fmt::println("Output: \n{}\n", out_stream.str());
    assert(ret == 10);
}

void test_multidim_init() {
    auto text = R"(
int main() {
    int arr[2][3] = {{1, 2, 3}, {4, 5}};
    print_int(arr[0][0]);
    print_int(arr[0][1]);
    print_int(arr[0][2]);
    print_int(arr[1][0]);
    print_int(arr[1][1]);
    print_int(arr[1][2]);
    return 0;
}
    )";
    auto stream = std::istringstream(text);
    fmt::println("Code: {}", text);
    auto code = ast::analysis(ast::parse(stream));
    auto prog = ir::gen::generate(code);
    fmt::println("Generated IR:\n{}", prog);
    auto out_stream = std::ostringstream{};
    auto env = ir::vm::VirtualMachine(std::cin, out_stream);
    int ret = env.execute(prog);
    fmt::println("Output: \n{}\n", out_stream.str());
    assert(ret == 0);
}

int main() {
    test_builtin();
    test_index();
    test_multidim_init();
    return 0;
}