#include "backend/ir/ir.hpp"
#include "backend/ir/vm/vm.h"
#include "backend/ir/gen/irgen.h"
#include "frontend/syntax/visit.hpp"

#include <sstream>
#include <cassert>

void test_builtin() {
    auto stream = std::istringstream(R"(
int main() {
    print_int(123);
    return 1;
}
    )");
    auto code = ast::analysis(ast::parse(stream));
    auto prog = ir::gen::generate(code);
    auto env = ir::vm::VirtualMachine(std::cin, std::cout);
    int ret = env.execute(prog);
    assert(ret == 1);
}

int main() {
    test_builtin();
    return 0;
}