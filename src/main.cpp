#include <iostream>
#define FMT_HEADER_ONLY
#include "fmt/format.h"
#include "frontend/syntax/error.hpp"
#include "frontend/syntax/visit.hpp"
#include "frontend/ast/analysis/semantic_ast.h"
#include "backend/ir/gen/irgen.h"
#include "utils/error.hpp"
#include "utils/tui.h"

#include <CACTLexer.h>
#include <CACTParser.h>

using namespace antlr4;

enum : uint8_t {
    SUCCESS = 0,
    INVALID_ARGUMENT = 1,
    SYNTAX_ERROR = 2,
    SEMANTIC_ERROR = 0, // for PR1, we do not need report semantic errors.
    RUNTIME_ERROR = 255,
};

int main(int argc, const char* argv[]) {
    int ret = SUCCESS;
    bool print_ast = false;
    bool print_ir = false;
    bool print_semantic = false;
    std::set<std::string> files;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--ast") {
            print_ast = true;
        } else if (arg == "--ir") {
            print_ir = true;
        } else if (arg == "--sem") {
            print_semantic = true;
        } else {
            files.insert(arg);
        }
    }
    try {
        if (argc < 2) {
            throw std::runtime_error(fmt::format("usage: {} [--ast] [--ir] [--sem] files ...", argv[0]));
        }

        for (const auto& file : files) {
            std::ifstream stream;
            stream.open(file);

            if (!stream.is_open()) {
                throw std::runtime_error(fmt::format("Failed to open file {}", file));
            }

            ANTLRInputStream input(stream);
            CACTLexer lexer(&input);
            CommonTokenStream tokens(&lexer);
            CACTParser parser(&tokens);

            CACTErrorListener listener;
            lexer.removeErrorListeners();
            lexer.addErrorListener(&listener);
            parser.removeErrorListeners();
            parser.addErrorListener(&listener);

            ASTVisitor visitor;
            try {
                std::unique_ptr<ast::CompUnit> ast(std::any_cast<ast::CompUnit*>(visitor.visit(parser.compUnit())));
                if (print_ast) {
                    fmt::println("AST:\n{}\n", ast);
                }
                auto semantic_ast = ast::SemanticAST(std::move(ast));
                if (print_semantic) {
                    fmt::println("Semantic analysis:\n");
                    semantic_ast.show();
                    fmt::println("\n");
                }
                auto program_ir = ir::gen::Generator().generate(semantic_ast);
                if (print_ir) {
                    fmt::println("IR:\n{}", program_ir);
                }
                fmt::println("{}: " BOLD GREEN "OK" NONE, file);
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
    }
    return ret;
}
