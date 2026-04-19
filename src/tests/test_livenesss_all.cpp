#include "backend/ir/gen/irgen.h"
#include "backend/ir/ir.h"
#include "backend/ir/optim/ssa.hpp"
#include "fmt/base.h"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/syntax/visit.hpp"
#include "utils/diagnosis.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

int main() {
    std::string base_path = "test/samples_codegen_functional";
    std::filesystem::path path(base_path);
    const int max_up = 10;
    int up = 0;
    while (!std::filesystem::exists(path) && up < max_up) {
        path = std::filesystem::path("..") / path;
        ++up;
    }
    if (!std::filesystem::exists(path)) {
        fmt::println("Directory not found after searching {} levels: {}\n", max_up, base_path);
        return 1;
    }
    fmt::println("Using directory: {}\n", path.string());
    for (const auto& file : std::filesystem::directory_iterator(path)) {
        if (file.path().extension() != ".cact") continue;
        fmt::println("Testing file: {}\n", file.path().string());
        auto fstream = std::fstream(file.path());
        auto text =
            std::string(std::istreambuf_iterator<char>(fstream), std::istreambuf_iterator<char>());
        fmt::println("Code: \n{}\n", text);
        auto istream = std::istringstream(text);
        auto ast = ast::analysis(ast::parse(istream));
        auto code = ir::gen::generate(ast);

        auto analysis = [](ir::Program& program, const std::string& name) {
            using namespace ir::analysis;
            fmt::println("{}: \n{}\n", name, program);
            std::unordered_set<const ir::Alloc*> globals;
            for (const auto& global : program.getGlobals()) {
                globals.insert(global.get());
            }
            for (auto& func_box : program.getFuncs()) {
                auto& func = *func_box;
                fmt::println("Function {}: \n", func.name);
                auto cfg = ControlFlowGraph(func);
                auto live = DataFlow<flow::Liveness>(cfg, program);
                fmt::println("{}", live.toString());
                std::unordered_set<const ir::Alloc*> params;
                for (const auto& local : func.params) {
                    params.insert(local.get());
                }
                auto entry = func.entrance();
                if (live.in.at(entry).size() == 0) {
                    fmt::println("  [OK] no live-in at entry\n");
                } else {
                    for (const auto& val : live.in.at(entry)) {
                        auto alloc = ir::optim::utils::alloc_of(val);
                        if (!alloc) {
                            fmt::println("  [FAIL] non-var live-in at entry: {}",
                                         live.in.at(entry));
                            exit(1);
                        }
                        if (globals.count(alloc)) {
                            fmt::println("  [OK] global '{}' is live-in at entry", alloc->name);
                        } else if (params.count(alloc)) {
                            fmt::println("  [OK] param '{}' is live-in at entry", alloc->name);
                        } else {
                            if (!alloc->init) {
                                fmt::println("  [FAIL] local value '{}' is live-in at entry "
                                             "without initializer",
                                             alloc->name);
                                exit(1);
                            } else {
                                fmt::println("  [OK] {}local value '{}' is initialized at entry",
                                             alloc->comptime ? "const " : "", alloc->name);
                            }
                        }
                    }
                    fmt::print("\n");
                }
            }
        };

        analysis(code, "Generated IR");

        auto to_ssa = ir::optim::ToSSA();
        to_ssa.apply(code);
        analysis(code, "SSA IR");

        auto ssa_to_temp = ir::optim::SSAValue2TempValue();
        ssa_to_temp.apply(code);
        analysis(code, "SSA IR, use TempValue");
    }
    return 0;
}