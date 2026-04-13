#include "backend/ir/gen/irgen.h"
#include "backend/ir/ir.hpp"
#include "backend/ir/optim/cfg.hpp"
#include "backend/ir/optim/dataflow/active_var.hpp"
#include "backend/ir/optim/dataflow/dominance.hpp"
#include "backend/ir/optim/dataflow/framework.hpp"
#include "backend/ir/optim/dominance.hpp"
#include "backend/ir/vm/vm.h"
#include "fmt/base.h"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/syntax/visit.hpp"
#include "utils/error.hpp"
#include "utils/tui.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

using namespace antlr4;

enum : uint8_t {
    SUCCESS = 0,
    INVALID_ARGUMENT = 1,
    SYNTAX_ERROR = 2,
    SEMANTIC_ERROR = 0,
    RUNTIME_ERROR = 255,
};

auto usage(const char* prog_name, int ret = 0) -> std::string {
    fmt::print(
        R"({} [--ast] [--ast-info] [--ir] [--exec] [--silent] files ... [--output <output file>] [--help]
    --ast       Print the AST of the input files
    --ast-info  Print the semantic analysis result of the AST
    --ir        Print the generated IR of the input files
    --ir-info   Print some analysis result of the generated IR
    --exec      Execute the generated IR
    --silent    Suppress all compiler output except the return value when executing
    --output    Write the generated IR also to the specified file
    --help      Show this help message
)",
        prog_name);
    exit(ret);
}

int main(int argc, const char* argv[]) {
    int ret = SUCCESS;

    bool print_ast = false;
    bool print_ir = false;
    bool print_ast_info = false;
    bool print_ir_info = false;
    bool execute = false;
    bool silent = false;
    FILE* output_file = nullptr;
    std::set<std::string> files;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--ast") {
            print_ast = true;
        } else if (arg == "--ir") {
            print_ir = true;
        } else if (arg == "--ast-info") {
            print_ast_info = true;
        } else if (arg == "--ir-info") {
            print_ir_info = true;
        } else if (arg == "--exec") {
            execute = true;
        } else if (arg == "--silent") {
            silent = true;
        } else if (arg == "--output") {
            if (i + 1 >= argc) {
                usage(argv[0], INVALID_ARGUMENT);
            }
            output_file = fopen(argv[++i], "w");
        } else if (arg == "--help") {
            usage(argv[0]);
        } else if (arg.length() > 1 && arg[0] == '-' && arg[1] == '-') {
            usage(argv[0], INVALID_ARGUMENT);
        } else {
            files.insert(arg);
        }
    }

    if (output_file && files.size() > 1) {
        fmt::println(stderr, "Warning: multiple input files, but only one output file specified. "
                             "Output will be overwritten.");
    }

    try {
        if (files.size() == 0) {
            usage(argv[0], INVALID_ARGUMENT);
        }

        for (const auto& file : files) {
            std::ifstream stream;
            stream.open(file);

            if (!stream.is_open()) {
                throw std::runtime_error(fmt::format("Failed to open file {}", file));
            }

            try {
                auto ast = ast::parse(stream);
                if (print_ast) {
                    fmt::println("AST:\n{}\n", ast);
                }

                auto code = ast::analysis(std::move(ast));
                if (print_ast_info) {
                    fmt::println("AST Semantic analysis:\n");
                    code.show();
                    fmt::println("\n");
                }

                auto program = ir::gen::generate(code);
                if (print_ir) {
                    if (!silent) {
                        fmt::println("Generated IR:\n");
                    }
                    fmt::println("{}", program);
                    if (output_file) {
                        fmt::println(output_file, "{}", program);
                    }
                    fmt::println("\n");
                }

                if (print_ir_info) {
                    using namespace ir::optim;
                    fmt::println("IR Analysis:\n");
                    for (auto& func : program.getFuncs()) {
                        fmt::println("function {}:", func->name);
                        auto cfg = ControlFlowGraph(*func);

                        auto dom = DataFlow<flow::Dominance>(cfg);
                        fmt::println("  (dominant blocks)\n{}", dom);

                        auto dom_tree = DominanceTree(dom);
                        fmt::println("  (immediate dominator)");
                        for (const auto& block : func->blocks()) {
                            fmt::println("    {}: {}", block->label,
                                         dom_tree.idom(block.get())
                                             ? dom_tree.idom(block.get())->label
                                             : "<null>");
                        }
                        fmt::print("\n");

                        auto dom_frontier = DominanceFrontier(cfg, dom_tree);
                        fmt::println("  (dominance frontier)");
                        for (const auto& block : func->blocks()) {
                            fmt::println("    {}: {}", block->label,
                                         dom_frontier.frontier(block.get()));
                        }
                        fmt::print("\n");

                        auto active = DataFlow<flow::ActiveVariables>(cfg);
                        fmt::println("  (active variables)\n{}", active);
                    }
                }

                if (execute) {
                    if (!silent) {
                        fmt::println("Executing program...");
                    }
                    ir::vm::VirtualMachine env(std::cin, std::cout);
                    uint8_t ret = env.execute(program);
                    if (!silent) {
                        fmt::println("Program returned {} after executing {} instructions", ret,
                                     env.perf().num_insts);
                    } else {
                        fmt::println("{}", ret);
                    }
                }

                if (!silent) {
                    fmt::println("{}: " BOLD GREEN "OK" NONE, file);
                }
            } catch (const SyntaxError& e) {
                fmt::println("{}: {}", file, e.what());
                ret |= SYNTAX_ERROR;
            } catch (const SemanticError& e) {
                fmt::println("{}: {}", file, e.what());
                ret |= SEMANTIC_ERROR;
            }
        }

    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << '\n';
        return RUNTIME_ERROR;
    } catch (const CompilerError& e) {
        std::cerr << e.what() << '\n';
        return RUNTIME_ERROR;
    }
    return ret;
}
