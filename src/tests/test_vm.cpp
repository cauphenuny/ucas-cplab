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
    assert(out_stream.str() == "123\n");
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
    assert(out_stream.str() == "-10\n");
}

void test_multidim_init() {
    auto text = R"(
void func(int a[][3]) {
    print_int(a[1][2]);
}
int main() {
    int arr[2][3] = {{1, 2}, {3, 4, 5}};
    int flat[2][3] = {1, 2, 3, 4, 5};
    print_int(arr[0][0]);
    print_int(arr[0][1]);
    print_int(arr[0][2]);
    print_int(arr[1][0]);
    print_int(arr[1][1]);
    print_int(arr[1][2]);
    print_int(flat[0][0]);
    print_int(flat[0][1]);
    print_int(flat[0][2]);
    print_int(flat[1][0]);
    print_int(flat[1][1]);
    print_int(flat[1][2]);
    func(arr);
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
    assert(out_stream.str() == "1\n2\n0\n3\n4\n5\n1\n2\n3\n4\n5\n0\n5\n");
}

void test_array_arg() {
    auto text = R"(
void print_arr(int arr[3]) {
    print_int(arr[0]);
    print_int(arr[1]);
    arr[2] = 42;
}
int main() {
    int arr[3] = {10, 20, 30};
    print_arr(arr);
    print_int(arr[2]);
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
    assert(out_stream.str() == "10\n20\n42\n");
}

void test_loop_local_decl_init() {
    auto text = R"(
int main() {
    int i = 0;
    int sum = 0;
    while (i < 5) {
        int j = 0;
        while (j < 2) {
            sum = sum + 1;
            j = j + 1;
        }
        i = i + 1;
    }
    print_int(sum);
    return sum;
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
    assert(ret == 10);
    assert(out_stream.str() == "10\n");
}

int main() {
    test_builtin();
    test_index();
    test_multidim_init();
    test_array_arg();
    test_loop_local_decl_init();
    return 0;
}