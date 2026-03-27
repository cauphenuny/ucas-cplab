#pragma once
#include <stdexcept>

#define FMT_HEADER_ONLY
#include "fmt/format.h"
#include "tui.h"

struct CodeError : std::runtime_error {
    CodeError(int line, int col, const std::string& desc, const std::string& type = "code error")
        : std::runtime_error(
              fmt::format(RED BOLD "{} " NONE "at {}:{} : {}", type, line, col, desc)) {}
};

struct SyntaxError : CodeError {
    SyntaxError(int line, int col, const std::string& desc,
                const std::string& type = "syntax error")
        : CodeError(line, col, desc, type) {}
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

struct SemanticError : CodeError {
    SemanticError(int line, int col, const std::string& desc,
                  const std::string& type = "semantic error")
        : CodeError(line, col, desc, type) {}
};

struct CompilerError : std::logic_error {
    CompilerError(const std::string& desc, const std::string& type = "compiler error")
        : std::logic_error(fmt::format(RED BOLD "{} " NONE ": {}", type, desc)) {}
};
