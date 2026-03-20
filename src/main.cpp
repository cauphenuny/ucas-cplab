#include <iostream>
#define FMT_HEADER_ONLY
#include "utils/error.h"
#include "utils/tui.h"

#include <fmt/format.h>

// #include "antlr4-runtime.h"
#include "CACTBaseVisitor.h"
#include "CACTLexer.h"
#include "CACTParser.h"
#include "frontend/syntax/listener.h"

using namespace antlr4;

enum : uint8_t {
    SUCCESS = 0,
    INVALID_ARGUMENT,
    SYNTAX_ERROR,
    TYPE_ERROR,
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

            CACTBaseVisitor visitor;
            try {
                visitor.visit(parser.compUnit());
                fmt::println("{}: " BOLD GREEN "OK" NONE, argv[i]);
            } catch (const SyntaxError& e) {
                fmt::println("{}: {}", argv[i], e.what());
                ret |= SYNTAX_ERROR;
            }
        }

    } catch (const std::runtime_error& e) {
        std::cerr << e.what() << '\n';
        return RUNTIME_ERROR;
    }
    return ret;
}
