#include <iostream>
#define FMT_HEADER_ONLY
#include "backend/ir/gen/irgen.h"
#include "backend/ir/vm/vm.h"
#include "fmt/format.h"
#include "frontend/ast/analysis/semantic_ast.h"
#include "frontend/syntax/visit.hpp"
#include "utils/error.hpp"
#include "utils/tui.h"

#include <CACTLexer.h>
#include <CACTParser.h>

using namespace antlr4;

enum : uint8_t {
    SUCCESS = 0,
    INVALID_ARGUMENT = 1,
    SYNTAX_ERROR = 2,
    SEMANTIC_ERROR = 0,  // for PR1, we do not need report semantic errors.
    RUNTIME_ERROR = 255,
};

int main(int argc, const char* argv[]) {
    int ret = SUCCESS;

    bool print_ast = false;
    bool print_ir = false;
    bool print_semantic = false;
    bool execute = false;
    bool silent = false;
    std::set<std::string> files;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--ast") {
            print_ast = true;
        } else if (arg == "--ir") {
            print_ir = true;
        } else if (arg == "--sem") {
            print_semantic = true;
        } else if (arg == "--exec") {
            execute = true;
        } else if (arg == "--silent") {
            silent = true;
        } else {
            files.insert(arg);
        }
    }

    try {
        if (files.size() == 0) {
            throw std::runtime_error(fmt::format(
                "usage: {} [--ast] [--sem] [--ir] [--exec] [--silent] files ...", argv[0]));
        }

        for (const auto& file : files) {
            std::ifstream stream;
            stream.open(file);

            if (!stream.is_open()) {
                throw std::runtime_error(fmt::format("Failed to open file {}", file));
            }

            try {
                auto code = ast::analysis(ast::parse(stream));
                if (print_ast) {
                    fmt::println("AST:\n{}\n", code.ast());
                }
                if (print_semantic) {
                    fmt::println("Semantic analysis:\n");
                    code.show();
                    fmt::println("\n");
                }

                auto program = ir::gen::generate(code);
                if (print_ir) {
                    fmt::println("IR:\n{}", program);
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
