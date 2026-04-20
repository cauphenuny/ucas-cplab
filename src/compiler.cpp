#include "backend/ir/analysis/cfg.hpp"
#include "backend/ir/analysis/dataflow/dominance.hpp"
#include "backend/ir/analysis/dataflow/framework.hpp"
#include "backend/ir/analysis/dataflow/liveness.hpp"
#include "backend/ir/analysis/dominance.hpp"
#include "backend/ir/gen/irgen.h"
#include "backend/ir/ir.h"
#include "backend/ir/optim/common_expr.hpp"
#include "backend/ir/optim/const_propagation.hpp"
#include "backend/ir/optim/copy_propagation.hpp"
#include "backend/ir/optim/dead_alloc.hpp"
#include "backend/ir/optim/dead_block.hpp"
#include "backend/ir/optim/dead_def.hpp"
#include "backend/ir/optim/inline.hpp"
#include "backend/ir/optim/ssa.hpp"
#include "backend/ir/vm/vm.h"
#include "fmt/base.h"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/syntax/visit.hpp"
#include "utils/diagnosis.hpp"
#include "utils/serialize.hpp"
#include "utils/tui.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
        R"({} [args]... files ...

    --help                  Show this help message

    --ast                   Print the AST of the input files
    --ast-info              Print the semantic analysis result of the AST

    --ir                    Print the generated IR
    --ir-info               Print analysis result of the generated IR
    --ssa                   Convert generated IR to SSA form
    --ssa2temp              Convert SSAValue in IR to TempValue

    --optimize-copy         Apply Copy Propagation optimization (triggers --ssa)
    --optimize-const        Apply Const Propagation optimization (triggers --ssa)
    --optimize-def          Apply Dead Definition Elimination optimization (triggers --ssa)
    --optimize-alloc        Apply Dead Allocation Elimination optimization (triggers --ssa, better with --ssa2temp)
    --optimize-block        Apply Dead/Trivial Block Elimination optimization (triggers --ssa)
    --optimize-inline [N=8] Apply Function Call Inlining optimization (threshold: N insts) (triggers --ssa)
    --optimize-exp          Apply Common Subexpression Elimination optimization (triggers --ssa)
    -O1, --optimize         Apply above optimizations, --no-optimize-[...] to disable specific optimizations

    --exec                  Execute the generated IR
    --silent                Suppress all compiler output except the return value when executing

    --output <file>         Write the generated IR also to the specified file

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

    bool optimize_alloc = false;
    bool optimize_def = false;
    bool optimize_exp = false;
    bool optimize_copy = false;
    bool optimize_const = false;
    bool optimize_block = false;

    std::vector<std::pair<std::string, std::reference_wrapper<bool>>> optimizations = {
        {"alloc", optimize_alloc}, {"def", optimize_def},     {"exp", optimize_exp},
        {"copy", optimize_copy},   {"const", optimize_const}, {"block", optimize_block},
    };

    size_t optimize_inline = 0;
    constexpr size_t default_inline_threshold = 8;

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
        } else if (arg == "-O1" || arg == "--optimize") {
            optimize_copy = true;
            optimize_const = true;
            optimize_def = true;
            optimize_alloc = true;
            optimize_exp = true;
            optimize_block = true;
            optimize_inline = default_inline_threshold;
        } else if (arg == "--optimize-inline" || arg == "--no-optimize-inline") {
            if (arg == "--no-optimize-inline") {
                optimize_inline = 0;
                continue;
            }
            if (i + 1 < argc && std::isdigit(argv[i + 1][0])) {
                optimize_inline = std::stoul(argv[++i]);
            } else {
                optimize_inline = default_inline_threshold;
            }
        } else if (arg == "--output") {
            if (i + 1 >= argc) {
                usage(argv[0], INVALID_ARGUMENT);
            }
            output_file = fopen(argv[++i], "w");
        } else if (arg == "--help") {
            usage(argv[0]);
        } else if (arg.length() > 1 && arg[0] == '-' && arg[1] == '-') {
            bool recognized = false;
            for (auto& [name, flag] : optimizations) {
                if (arg == "--optimize-" + name) {
                    flag.get() = true, recognized = true;
                    break;
                } else if (arg == "--no-optimize-" + name) {
                    flag.get() = false, recognized = true;
                    break;
                }
            }
            if (!recognized) {
                fmt::println(stderr, "unknown option: {}\n", arg);
                usage(argv[0], INVALID_ARGUMENT);
            }
        } else {
            files.insert(arg);
        }
    }

    if (output_file && files.size() > 1) {
        warning("multiple input files, but only one output file specified. Output will be "
                "overwritten.");
    }

    bool optimize = optimize_def || optimize_exp || optimize_copy || optimize_alloc ||
                    optimize_const || optimize_inline;
    if (optimize && !to_ssa) {
        // warning("Optimization requires SSA form. Auto enabling SSA form.");
        to_ssa = true;
    }

    if (optimize && !silent) {
        fmt::print("optimizations: ");
        for (auto& [name, flag] : optimizations) {
            fmt::print("{}{} ", flag.get() ? "+" : "-", name);
        }
        fmt::println("{}{} ", optimize_inline > 0 ? "+" : "-", "inline");
        fmt::print("\n");
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

                auto echo = [&](const ir::Program& program, const std::string& name) {
                    if (print_ir) {
                        fmt::println("{}:\n{}\n", name, program);
                    }
                    if (print_ir_info) {
                        analysis(program);
                    }
                };

                size_t pass_id = 0;

                auto apply =
                    [&](ir::Program& program,
                        const std::vector<std::pair<std::unique_ptr<ir::optim::Pass>, std::string>>&
                            passes) {
                        bool any_changed = false;
                        for (auto& [pass, name] : passes) {
                            try {

                                bool pass_changed = pass->apply(program);
                                if (pass_changed) {
                                    echo(program, fmt::format("#{} {}", pass_id++, name));
                                }
                                any_changed |= pass_changed;
                            } catch (const CompilerError& e) {
                                fmt::println("Error during pass '{}': {}", name, e.what());
                                exit(RUNTIME_ERROR);
                            }
                        }
                        return any_changed;
                    };

                echo(program, "Generated IR");

                {
                    using namespace ir::optim;
                    std::vector<std::pair<std::unique_ptr<Pass>, std::string>> passes;
                    if (to_ssa) {
                        passes.emplace_back(std::make_unique<ConstructSSA>(), "SSA Form");
                    }
                    if (ssa_to_temp) {
                        passes.emplace_back(std::make_unique<SSAValue2TempValue>(),
                                            "SSA Form (TempValue)");
                    }

                    apply(program, passes);
                }

                if (optimize) {
                    using namespace ir::optim;
                    std::vector<std::pair<std::unique_ptr<Pass>, std::string>> passes;
                    if (optimize_copy) {
                        passes.emplace_back(std::make_unique<CopyPropagation>(),
                                            "Copy Propagation");
                    }
                    if (optimize_const) {
                        passes.emplace_back(std::make_unique<ConstPropagation>(),
                                            "Const Propagation");
                    }
                    if (optimize_def) {
                        passes.emplace_back(std::make_unique<DeadDefElimination>(),
                                            "Dead Definition Elimination");
                    }
                    if (optimize_alloc) {
                        passes.emplace_back(std::make_unique<DeadAllocElimination>(),
                                            "Dead Allocation Elimination");
                    }
                    if (optimize_exp) {
                        passes.emplace_back(std::make_unique<DeadBlockElimination>(),
                                            "Dead Block Elimination");
                        passes.emplace_back(std::make_unique<CommonSubexprElimination>(),
                                            "Common Subexpression Elimination");
                    }
                    if (optimize_block) {
                        passes.emplace_back(std::make_unique<SimplifyCFG>(), "CFG Simplification");
                        passes.emplace_back(std::make_unique<DeadBlockElimination>(),
                                            "Dead Block Elimination");
                    }
                    if (optimize_inline) {
                        passes.emplace_back(std::make_unique<Inlining>(optimize_inline),
                                            "Function Call Inlining");
                    }
                    while (apply(program, passes));
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
