#include "backend/ir/analysis/cfg.hpp"
#include "backend/ir/analysis/dataflow/dominance.hpp"
#include "backend/ir/analysis/dataflow/framework.hpp"
#include "backend/ir/analysis/dataflow/liveness.hpp"
#include "backend/ir/analysis/dominance.hpp"
#include "backend/ir/gen/irgen.h"
#include "backend/ir/ir.h"
#include "backend/ir/lowering/addr.hpp"
#include "backend/ir/lowering/array.hpp"
#include "backend/ir/lowering/proxy.hpp"
#include "backend/ir/lowering/reg2mem.hpp"
#include "backend/ir/lowering/regalloc/main.hpp"
#include "backend/ir/lowering/stdlib.hpp"
#include "backend/ir/transform/framework.hpp"
#include "backend/ir/transform/optim/common_expr.hpp"
#include "backend/ir/transform/optim/constant_fold.hpp"
#include "backend/ir/transform/optim/copy_propagation.hpp"
#include "backend/ir/transform/optim/dead_alloc.hpp"
#include "backend/ir/transform/optim/dead_block.hpp"
#include "backend/ir/transform/optim/dead_def.hpp"
#include "backend/ir/transform/optim/inline.hpp"
#include "backend/ir/transform/ssa/construct.hpp"
#include "backend/ir/transform/ssa/destruct.hpp"
#include "backend/ir/vm/vm.h"
#include "backend/rv64/abi.hpp"
#include "backend/rv64/isel.hpp"
#include "backend/rv64/optim/label.hpp"
#include "backend/rv64/optim/peephole.hpp"
#include "backend/rv64/vm/vm.hpp"
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

    -o, --output <file>     Write the generated IR or assembly to the specified file

    --ast                   Print the AST of the input files
    --ast-info              Print the semantic analysis result of the AST

    --ir                    Print the generated IR
    --ir-info               Print analysis result of the generated IR

    --retain-ssa-value      Do not convert SSAValue to TempValue in IR optimization passes

    --optimize-copy         Apply Copy Propagation optimization
    --optimize-const        Apply Constant Folding optimization
    --optimize-def          Apply Dead Definition Elimination optimization
    --optimize-alloc        Apply Dead Allocation Elimination optimization
    --optimize-temp         Apply Dead Temporary Value Elimination optimization
    --optimize-block        Apply Dead/Trivial Block Elimination optimization
    --optimize-inline [N=8] Apply Function Call Inlining optimization (threshold: N insts)
    --optimize-exp          Apply Common Subexpression Elimination optimization
    --optimize-asm          Apply optimizations after assembly code generation
    -O1, -O2, --optimize    Apply above optimizations, --no-optimize-[...] to disable specific optimizations

    --lowering-addr         Apply array-index lowering
    --lowering-proxy        Apply access proxy insertion (create temp value proxy which is reg-allocatable)
    --lowering-array        Apply array initialization lowering (lower array store to memset/memcpy)
    --lowering-reg          Apply register allocation
    --lowering-prune        Apply redundant move elimination after register allocation
    --lowering-optim        Apply optimizations after lowering transformations
    --lowering              Apply above lowering transformations

    --exec                  Execute the generated IR
    --silent                Suppress all compiler output except the return value when executing

    --exec-debug            Enable debug mode in execution (add breakpoints, execute step by step, etc.)
    --exec-trace            Trace execution with detailed instruction and block information

    -S, --asm               Generate and output RV64 assembly code (implies --lowering)
    --asm-exec              Execute the generated assembly code (implies --asm)
)",
        prog_name);
    exit(ret);
}

auto analysis(const ir::Program& program) {
    using namespace ir::analysis;
    fmt::println("IR Analysis:\n");
    for (auto& func : program.funcs()) {
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
    std::cout << std::unitbuf;

    int ret = SUCCESS;

    bool print_ast = false;
    bool print_ir = false;
    bool print_ast_info = false;
    bool print_ir_info = false;
    bool print_asm = false;

    bool retain_ssa_value = false;

    bool optimize_alloc = false;
    bool optimize_def = false;
    bool optimize_exp = false;
    bool optimize_copy = false;
    bool optimize_const = false;
    bool optimize_block = false;
    bool optimize_temp = false;
    bool optimize_asm = false;

    bool lowering_addr = false;
    bool lowering_proxy = false;
    bool lowering_reg = false;
    bool lowering_prune = false;
    bool lowering_optim = false;
    bool lowering_array = false;

    bool execute = false;
    bool execute_trace = false;
    bool execute_debug = false;
    bool silent = false;

    bool assembly_exec = false;

    std::vector<std::pair<std::string, std::reference_wrapper<bool>>> optimizations = {
        {"alloc", optimize_alloc}, {"def", optimize_def},     {"exp", optimize_exp},
        {"copy", optimize_copy},   {"const", optimize_const}, {"block", optimize_block},
        {"temp", optimize_temp},   {"asm", optimize_asm}};

    std::vector<std::pair<std::string, std::reference_wrapper<bool>>> lowerings = {
        {"addr", lowering_addr},   {"proxy", lowering_proxy}, {"reg", lowering_reg},
        {"prune", lowering_prune}, {"optim", lowering_optim}, {"array", lowering_array}};

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
        } else if (arg == "--retain-ssa-value") {
            retain_ssa_value = true;
        } else if (arg == "--exec") {
            execute = true;
        } else if (arg == "--silent") {
            silent = true;
        } else if (arg == "--exec-debug") {
            execute_debug = true;
        } else if (arg == "--exec-trace") {
            execute_trace = true;
        } else if (arg == "--asm-exec") {
            assembly_exec = true;
            // --asm-exec implies --asm
            print_asm = true;
            for (auto& [name, flag] : lowerings) flag.get() = true;
        } else if (arg == "-S" || arg == "--asm") {
            print_asm = true;
            // -S/--asm implies --lowering
            for (auto& [name, flag] : lowerings) flag.get() = true;
        } else if (arg == "--lowering") {
            for (auto& [name, flag] : lowerings) flag.get() = true;
        } else if (arg == "-O0") {
            for (auto& [name, flag] : optimizations) flag.get() = false;
            optimize_inline = 0;
        } else if (arg == "--optimize" || arg == "-O1" || arg == "-O2") {
            for (auto& [name, flag] : optimizations) flag.get() = true;
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
        } else if (arg == "-o" || arg == "--output") {
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
            for (auto& [name, flag] : lowerings) {
                if (arg == "--lowering-" + name) {
                    flag.get() = true, recognized = true;
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

    if (output_file && (!print_ir && !print_asm)) {
        warning("output file specified, but neither --ir nor --asm is enabled. No output will "
                "be written.");
    }

    if (output_file) {
        silent = true;
    }

    bool optimize = optimize_def || optimize_exp || optimize_copy || optimize_alloc ||
                    optimize_const || optimize_inline || optimize_temp;
    bool lowering = lowering_addr || lowering_reg || lowering_prune || lowering_optim ||
                    lowering_proxy || lowering_array;

    if (optimize && !silent) {
        std::stringstream ss;
        ss << "enabled optimizations: ";
        for (auto& [name, flag] : optimizations) {
            ss << fmt::format("{}{} ", flag.get() ? "+" : "-", name);
        }
        ss << fmt::format("{}{} ", optimize_inline > 0 ? "+" : "-", "inline");
        info(ss.str());
    }

    if (lowering && !silent) {
        std::stringstream ss;
        ss << "enabled lowering transformations: ";
        for (auto& [name, flag] : lowerings) {
            ss << fmt::format("{}{} ", flag.get() ? "+" : "-", name);
        }
        info(ss.str());
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

                if (print_ast || print_ast_info) fmt::println("---\n");

                auto program_box = ir::gen::generate(code);
                auto& program = *program_box;

                size_t pass_id = 0;

                auto echo = [&](const ir::Program& program, const std::string& name) {
                    if (!silent)
                        fmt::print("{}. {}{}", ++pass_id, name,
                                   (print_ir) ? fmt::format(":\n\n```rust\n{}\n```\n\n", program)
                                              : "\n");
                    if (print_ir_info) {
                        analysis(program);
                    }
                };

                auto apply = [&](ir::Program& program, auto& ctx, const auto& passes) {
                    bool any_changed = false;
                    for (auto& [pass, name] : passes) {
                        try {

                            bool pass_changed = pass->apply(program, ctx);
                            if (pass_changed) {
                                echo(program, name);
                            }
                            any_changed |= pass_changed;
                        } catch (const CompilerError& e) {
                            fmt::println("Error during pass '{}': {}", name, e.what());
                            exit(RUNTIME_ERROR);
                        }
                    }
                    return any_changed;
                };

                if (!silent) fmt::println("IR transformations:\n");
                echo(program, "Generated IR");

                {
                    using namespace ir::transform;
                    using namespace ir::lowering;
                    ConstructSSA().apply(program);
                    echo(program, "Construct SSA");
                    SSAPassContext ctx(program);
                    if (lowering) {
                        AddStandardLib<SSAPassContext>().apply(program, ctx);
                    }
                    if (optimize || lowering) {
                        using namespace ir::transform;
                        std::vector<std::pair<std::unique_ptr<SSAPass>, std::string>> passes;
                        if (!retain_ssa_value) {
                            passes.emplace_back(
                                std::make_unique<SSAValue2TempValue<SSAPassContext>>(),
                                "SSAValue to TempValue");
                        }
                        if (optimize_copy) {
                            passes.emplace_back(std::make_unique<CopyPropagation>(),
                                                "Copy Propagation");
                        }
                        if (optimize_const) {
                            passes.emplace_back(std::make_unique<ConstantFolding<SSAPassContext>>(),
                                                "Constant Folding");
                        }
                        if (optimize_def) {
                            passes.emplace_back(std::make_unique<DeadDefElimination>(),
                                                "Dead Definition Elimination");
                        }
                        if (optimize_alloc) {
                            passes.emplace_back(
                                std::make_unique<DeadAllocElimination<SSAPassContext>>(),
                                "Dead Allocation Elimination");
                        }
                        if (optimize_temp) {
                            passes.emplace_back(
                                std::make_unique<DeadTempElimination<SSAPassContext>>(),
                                "Dead Temporary Value Elimination");
                        }
                        if (optimize_exp) {
                            passes.emplace_back(
                                std::make_unique<DeadBlockElimination<SSAPassContext>>(),
                                "Dead Block Elimination");
                            passes.emplace_back(std::make_unique<CommonSubexprElimination>(),
                                                "Common Subexpression Elimination");
                        }
                        if (optimize_block) {
                            passes.emplace_back(std::make_unique<SimplifyCFG<SSAPassContext>>(),
                                                "CFG Simplification");
                            passes.emplace_back(
                                std::make_unique<DeadBlockElimination<SSAPassContext>>(),
                                "Dead Block Elimination");
                        }
                        if (optimize_inline) {
                            passes.emplace_back(std::make_unique<Inlining>(optimize_inline),
                                                "Function Call Inlining");
                        }
                        if (lowering_addr) {
                            passes.emplace_back(std::make_unique<AddressLowering>(rv64::ABI),
                                                "Address Lowering");
                        }
                        while (apply(program, ctx, passes));
                    }
                }

                if (lowering) {

                    using namespace ir::lowering;
                    using namespace ir::transform;
                    using Context = ir::transform::NonSSAPassContext;
                    Context ctx(program);

                    DestructSSA().apply(program, ctx);
                    echo(program, "Destruct SSA");

                    ColorMap regs;

                    if (lowering_proxy) {
                        bool changed = AccessProxyLowering<Context>().apply(program, ctx);
                        if (changed) echo(program, "Access Proxy Lowering");
                    }

                    if (lowering_array) {
                        bool changed = ArrayInitLowering<Context>(rv64::ABI).apply(program, ctx);
                        if (changed) echo(program, "Array Initialization Lowering");
                    }

                    if (lowering_reg) {
                        RegToMem(rv64::ABI).apply(program, ctx);
                        echo(program, "Register to Memory");

                        auto regalloc = RegisterAllocation(rv64::ABI);
                        regalloc.apply(program, ctx);
                        RegisterReplacement<Context>(regalloc.colored, regalloc.precolored)
                            .apply(program, ctx);

                        echo(program, "Register Allocation");

                        for (auto& [key, alloc] : regalloc.precolored)
                            regs[alloc->value()] = key.second;
                    }

                    if (lowering_prune) {
                        RedundantMoveElimination<Context>().apply(program, ctx);
                        echo(program, "Redundant Move Elimination");
                    }

                    {
                        std::vector<std::pair<std::unique_ptr<NonSSAPass>, std::string>> passes;
                        if (lowering_optim) {
                            if (optimize_alloc)
                                passes.emplace_back(
                                    std::make_unique<DeadAllocElimination<Context>>(),
                                    "Dead Allocation Elimination");
                            if (optimize_temp)
                                passes.emplace_back(
                                    std::make_unique<DeadTempElimination<Context>>(),
                                    "Dead Temporary Elimination");
                            if (optimize_block) {
                                passes.emplace_back(
                                    std::make_unique<DeadBlockElimination<Context>>(),
                                    "Dead Block Elimination");
                                passes.emplace_back(std::make_unique<SimplifyCFG<Context>>(),
                                                    "CFG Simplification");
                            }
                        }
                        if (print_asm) {
                            passes.emplace_back(
                                std::make_unique<ConstantFolding<Context>>(),
                                "Constant Folding");  // riscv asm only allows one immediate operand
                        }
                        while (apply(program, ctx, passes));
                    }

                    if (!silent) fmt::println("\n---\n");

                    if (print_asm) {
                        auto module = rv64::isel::lower(program, regs);

                        if (!silent)
                            fmt::println("Generated RV64 Assembly:\n\n```asm\n{}\n```\n", module);

                        if (optimize_asm) {
                            bool changed = false;
                            changed |= rv64::optim::RedundantJumpElimination().apply(module);
                            changed |= rv64::optim::DeadLabelElimination().apply(module);
                            if (!silent && changed) {
                                fmt::println("After Optimizations:\n\n```asm\n{}\n```\n", module);
                            }
                        }

                        if (output_file) {
                            fmt::print(output_file, "{}", module);
                        }

                        if (assembly_exec) {
                            rv64::vm::VirtualMachine vm{std::cin, std::cout};
                            uint8_t ret = vm.execute(module);
                            if (!silent) {
                                fmt::println("Program returned {} after executing {} instructions",
                                             ret, vm.num_insts);
                            } else {
                                fmt::println("{}", ret);
                            }
                        }
                    }
                }

                if (execute) {
                    if (!silent) {
                        fmt::println("Executing program...");
                    }
                    ir::vm::VirtualMachine env(std::cin, std::cout);
                    uint8_t ret =
                        env.execute(program, execute_trace ? &std::cout : nullptr, execute_debug);
                    if (!silent) {
                        fmt::println("Program returned {} after executing {} instructions", ret,
                                     env.perf().num_insts);
                    } else {
                        fmt::println("{}", ret);
                    }
                }

                if (output_file && print_ir && !print_asm) {
                    fmt::println(output_file, "{}", program);
                }

                if (!silent) {
                    info(fmt::format("{}: " BOLD GREEN "OK" NONE, file));
                }

            } catch (const SyntaxError& e) {
                error(fmt::format("{}: {}", file, e.what()));
                ret |= SYNTAX_ERROR;
            } catch (const SemanticError& e) {
                error(fmt::format("{}: {}", file, e.what()));
                ret |= SEMANTIC_ERROR;
            }
        }
    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << '\n';
        return RUNTIME_ERROR;
    }
    return ret;
}
