#pragma once
#include <stdexcept>

#define FMT_HEADER_ONLY
#include "fmt/format.h"
#include "tui.h"

struct SyntaxError : std::runtime_error {
    SyntaxError(int line, int col, const std::string& desc,
                const std::string& type = "syntax error")
        : std::runtime_error(
              fmt::format(RED BOLD "{} " NONE "at {}:{} : {}", type, line, col, desc)) {}
    SyntaxError(int line, int col, const std::string& type = "syntax error")
        : std::runtime_error(fmt::format(RED BOLD "{} " NONE "at {}:{}", type, line, col)) {}
};

struct LexicalError : SyntaxError {
    using SyntaxError::SyntaxError;
    LexicalError(int line, int col, const std::string& desc)
        : SyntaxError(line, col, desc, "tokenize error") {}
};

struct SyntacticError : SyntaxError {
    using SyntaxError::SyntaxError;
    SyntacticError(int line, int col, const std::string& desc)
        : SyntaxError(line, col, desc, "parse error") {}
};