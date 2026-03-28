#include <iostream>
#define FMT_HEADER_ONLY
#include "fmt/format.h"
#include "frontend/syntax/error.h"
#include "frontend/syntax/visit.h"
#include "backend/ir/semantic.h"
#include "utils/error.h"
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
    try {
        if (argc < 2) {
            throw std::runtime_error(fmt::format("usage: {} files ...", argv[0]));
        }

        for (int i = 1; i < argc; i++) {
            std::ifstream stream;
            stream.open(argv[i]);

            if (!stream.is_open()) {
                throw std::runtime_error(fmt::format("Failed to open file {}", argv[i]));
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
                auto res = visitor.visit(parser.compUnit());
                auto comp_unit = ASTVisitor::take<ast::CompUnit>(res);
                fmt::println("AST: \n{}\n\n", comp_unit);
                auto semantic_ast = ir::SemanticAST(std::move(comp_unit));
                fmt::println("Semantic analysis:\n");
                semantic_ast.show();
                fmt::println("{}: " BOLD GREEN "OK" NONE, argv[i]);
            } catch (const SyntaxError& e) {
                fmt::println("{}: {}", argv[i], e.what());
                ret |= SYNTAX_ERROR;
            } catch (const SemanticError& e) {
                fmt::println("{}: {}", argv[i], e.what());
                ret |= SEMANTIC_ERROR;
            }
        }

    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << '\n';
        return RUNTIME_ERROR;
    }
    return ret;
}
