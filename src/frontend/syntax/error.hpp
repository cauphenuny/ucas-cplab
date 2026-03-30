#pragma once

#include "BaseErrorListener.h"
#include "Lexer.h"
#include "utils/traits.hpp"
#include "utils/error.hpp"

#include <cstddef>
#include <string>

class CACTErrorListener : public antlr4::BaseErrorListener {
public:
    void syntaxError(antlr4::Recognizer* recognizer, antlr4::Token* offendingSymbol, size_t line,
                     size_t charPositionInLine, const std::string& msg,
                     std::exception_ptr e) override {
        if (dynamic_cast<antlr4::Lexer*>(recognizer)) {
            throw LexicalError(Location{(int)line, (int)charPositionInLine}, msg);
        } else {
            throw SyntacticError(Location{(int)line, (int)charPositionInLine}, msg);
        }
    }
};
