#include "backend/ir/analysis/cfg.hpp"
#include "backend/ir/analysis/dataflow/dominance.hpp"
#include "backend/ir/analysis/dataflow/framework.hpp"
#include "backend/ir/analysis/dataflow/liveness.hpp"
#include "backend/ir/analysis/dominance.hpp"
#include "backend/ir/gen/irgen.h"
#include "backend/ir/ir.hpp"
#include "backend/ir/optim/ssa.hpp"
#include "backend/ir/vm/vm.h"
#include "fmt/base.h"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/syntax/visit.hpp"
#include "utils/diagnosis.hpp"
#include "utils/serialize.hpp"
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
    SEMANTIC_ERROR = 4,
    RUNTIME_ERROR = 255,
};

auto usage(const char* prog_name, int ret = 0) -> std::string {
    fmt::print(
        R"({} [--ast] [--ast-info] [--ir] [--ir-info] [--ssa] [--to-temp] [--exec] [--silent] files ... [--output <output file>] [--help]

    --help      Show this help message

    --ast       Print the AST of the input files
    --ast-info  Print the semantic analysis result of the AST

    --ir        Print the generated IR of the input files
    --ir-info   Print some analysis result of the generated IR
    --ssa       Convert generated IR to SSA form
    --ssa2temp  Convert SSAValue in IR to TempValue, then prune useless allocation

    --exec      Execute the generated IR
    --silent    Suppress all compiler output except the return value when executing

    --output    Write the generated IR also to the specified file

)",
        prog_name);
    exit(ret);
}

auto analysis(const ir::Program& program) {
    using namespace ir::analysis;
    fmt::println("IR Analysis:\n");
    for (auto& func : program.getFuncs()) {
        auto _ = fmt_indent::Guard();
        auto ind = fmt_indent::state.indent();

        fmt::println("function {}:", func->name);
        auto cfg = ControlFlowGraph(*func);

        auto dom = DataFlow<flow::Dominance>(cfg, program);
        {
            auto _ = fmt_indent::Guard();
            fmt::println("{}(dominant blocks)\n{}", ind, dom);
        }

        auto dom_tree = DominanceTree(dom);
        {
            auto _ = fmt_indent::Guard();
            fmt::println("{}(immediate dominator)", ind);
            for (const auto& block : func->blocks()) {
                fmt::println("{}{}: {}", fmt_indent::state.indent(), block->label,
                             dom_tree.idom(block.get()) ? dom_tree.idom(block.get())->label
                                                        : "<null>");
            }
            fmt::print("\n");
        }

        auto dom_frontier = DominanceFrontier(cfg, dom_tree);
        {
            auto _ = fmt_indent::Guard();
            fmt::println("{}(dominance frontier)", ind);
            for (const auto& block : func->blocks()) {
                fmt::println("{}{}: {}", fmt_indent::state.indent(), block->label,
                             dom_frontier.frontier(block.get()));
            }
            fmt::print("\n");
        }

        auto live = DataFlow<flow::Liveness>(cfg, program);
        {
            auto _ = fmt_indent::Guard();
            fmt::println("{}(live variables)\n{}", ind, live);
        }
    }
}

int main(int argc, const char* argv[]) {
    int ret = SUCCESS;

    bool print_ast = false;
    bool print_ir = false;
    bool print_ast_info = false;
    bool print_ir_info = false;
    bool execute = false;
    bool silent = false;
    bool to_ssa = false;
    bool ssa_to_temp = false;
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
        } else if (arg == "--ssa") {
            to_ssa = true;
        } else if (arg == "--ssa2temp") {
            ssa_to_temp = true;
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
                    fmt::println("\n");
                }
                if (print_ir_info) {
                    analysis(program);
                }

                {
                    using namespace ir::optim;
                    std::vector<std::pair<std::unique_ptr<Pass>, std::string>> passes;
                    if (to_ssa) {
                        passes.emplace_back(std::make_unique<ToSSA>(), "SSA Form");
                    }
                    if (ssa_to_temp) {
                        passes.emplace_back(std::make_unique<SSAValue2TempValue>(),
                                            "SSA Form (TempValue)");
                    }

                    for (auto& [pass, name] : passes) {
                        pass->apply(program);
                        if (print_ir) {
                            fmt::println("{}:\n{}\n", name, program);
                        }
                        if (print_ir_info) {
                            analysis(program);
                        }
                    }
                }

                if (output_file) {
                    if (print_ir) {
                        fmt::println(output_file, "{}", program);
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
