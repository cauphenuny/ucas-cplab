#include "backend/ir/analysis/cfg.hpp"
#include "backend/ir/analysis/dataflow/dominance.hpp"
#include "backend/ir/analysis/dataflow/framework.hpp"
#include "backend/ir/ir.h"
#include "backend/ir/parse/visit.hpp"
#include "fmt/base.h"

#include <functional>
#include <sstream>

using namespace ir::analysis;
using Dominance = flow::Dominance;

void test(const std::string& name, const std::string& text,
          const std::function<void(const ir::Program&, const ir::Func&,
                                   const DataFlow<Dominance>&)>& verify = nullptr) {
    fmt::println("Test: {}", name);
    auto ir_stream = std::istringstream(text);
    try {
        auto program = ir::parse(ir_stream);
        for (auto& func_ptr : program.getFuncs()) {
            auto& func = *func_ptr;
            fmt::println("  Function: {}", func.name);
            auto cfg = ControlFlowGraph(func);
            auto dom_analysis = DataFlow<Dominance>(cfg, program);
            fmt::println("{}", dom_analysis.toString());
            if (verify) {
                verify(program, func, dom_analysis);
            }
        }
    } catch (const std::exception& e) {
        fmt::println("  Error during test '{}': {}", name, e.what());
    }
    fmt::println("------------------------------------------");
}

int main() {
    auto check = [](bool cond, const std::string& msg) {
        if (cond) {
            fmt::println("    [OK] {}", msg);
        } else {
            fmt::println("    [FAIL] {}", msg);
        }
    };

    // 1. Simple Diamond
    test("Simple Diamond",
         R"(
fn main() -> i32 {
    'entry: {
        $0: bool = true;
        branch $0 ? 'then_blk : 'else_blk;
    }
    'then_blk: {
        jump 'exit_blk;
    }
    'else_blk: {
        jump 'exit_blk;
    }
    'exit_blk: {
        return 0;
    }
}
)",
         [&](const auto& prog, const auto& func, const auto& dom) {
             auto entry = func.findBlock("entry");
             auto then_blk = func.findBlock("then_blk");
             auto else_blk = func.findBlock("else_blk");
             auto exit_blk = func.findBlock("exit_blk");

             // For forward dominance, 'out' set is the dominators of that block
             check(dom.out.at(entry).contains(entry), "entry dominates entry");
             check(dom.out.at(then_blk).contains(entry), "entry dominates then");
             check(dom.out.at(else_blk).contains(entry), "entry dominates else");
             check(dom.out.at(exit_blk).contains(entry), "entry dominates exit");
             check(!dom.out.at(exit_blk).contains(then_blk), "then does not dominate exit");
             check(!dom.out.at(exit_blk).contains(else_blk), "else does not dominate exit");
         });

    // 2. Loop
    test("Simple Loop",
         R"(
fn main() -> i32 {
    'entry: {
        jump 'cond;
    }
    'cond: {
        $0: bool = true;
        branch $0 ? 'body : 'exit;
    }
    'body: {
        jump 'cond;
    }
    'exit: {
        return 0;
    }
}
)",
         [&](const auto& prog, const auto& func, const auto& dom) {
             auto entry = func.findBlock("entry");
             auto cond = func.findBlock("cond");
             auto body = func.findBlock("body");
             auto exit = func.findBlock("exit");

             check(dom.out.at(cond).contains(entry), "entry dominates cond");
             check(dom.out.at(cond).contains(cond), "cond dominates itself");
             check(dom.out.at(body).contains(cond), "cond dominates body");
             check(dom.out.at(exit).contains(cond), "cond dominates exit");
             check(!dom.out.at(cond).contains(body), "body does not dominate cond");
         });

    return 0;
}
