#include "backend/ir/ir.hpp"
#include "backend/ir/parse/visit.hpp"
#include "backend/ir/vm/vm.h"
#include "fmt/base.h"
#include "utils/diagnosis.hpp"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

enum : uint8_t {
    SUCCESS = 0,
    INVALID_ARGUMENT = 1,
    SYNTAX_ERROR = 2,
    SEMANTIC_ERROR = 4,
    RUNTIME_ERROR = 255,
};

auto usage(const char* prog_name, int ret = 0) -> std::string {
    fmt::print(
        R"({} [--help] [--silent] [--print] IR_file
    --help      Show this help message
    --print     Show reconstructed IR, without executing
    --silent    Suppress all compiler output except the return value when executing
)",
        prog_name);
    exit(ret);
}

int main(int argc, const char* argv[]) {
    int ret = SUCCESS;

    std::string file;
    bool silent = false;
    bool print = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--help") {
            usage(argv[0]);
        } else if (arg == "--silent") {
            silent = true;
        } else if (arg == "--print") {
            print = true;
        } else if (arg.length() > 1 && arg[0] == '-' && arg[1] == '-') {
            usage(argv[0], INVALID_ARGUMENT);
        } else {
            file = arg;
        }
    }

    try {
        if (file.empty()) {
            usage(argv[0], INVALID_ARGUMENT);
        }

        std::ifstream stream;
        stream.open(file);

        if (!stream.is_open()) {
            throw std::runtime_error(fmt::format("Failed to open file {}", file));
        }

        try {
            auto program = ir::parse(stream);

            if (print) {
                fmt::println("{}", program);
            } else {
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

        } catch (const SyntaxError& e) {
            fmt::println("{}: {}", file, e.what());
            ret |= SYNTAX_ERROR;
        } catch (const SemanticError& e) {
            fmt::println("{}: {}", file, e.what());
            ret |= SEMANTIC_ERROR;
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
