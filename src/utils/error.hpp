#pragma once
#include <stdexcept>
#include <string>

#define FMT_HEADER_ONLY
#include "fmt/format.h"
#include "traits.hpp"
#include "tui.h"

struct CodeError : std::runtime_error {
    CodeError(Location loc, const std::string& desc, const std::string& type = "code error")
        : std::runtime_error(
              fmt::format(RED BOLD "{} " NONE "at {}:{} : {}", type, loc.line, loc.col, desc)) {}
};

struct SyntaxError : CodeError {
    SyntaxError(Location loc, const std::string& desc,
                const std::string& type = "syntax error")
        : CodeError(loc, desc, type) {}
};

struct LexicalError : SyntaxError {
    using SyntaxError::SyntaxError;
    LexicalError(Location loc, const std::string& desc)
        : SyntaxError(loc, desc, "tokenize error") {}
};

struct SyntacticError : SyntaxError {
    using SyntaxError::SyntaxError;
    SyntacticError(Location loc, const std::string& desc)
        : SyntaxError(loc, desc, "parse error") {}
};

struct SemanticError : CodeError {
    SemanticError(Location loc, const std::string& desc,
                  const std::string& type = "semantic error")
        : CodeError(loc, desc, type) {}
};

struct CompilerError : std::logic_error {
    CompilerError(const std::string& desc, const std::string& type = "compiler error")
        : std::logic_error(fmt::format(RED BOLD "{} " NONE ": {}", type, desc)) {}
    CompilerError(std::string_view file, int line, const std::string& desc,
                  const std::string& type = "compiler error")
        : std::logic_error(fmt::format(RED BOLD "{} " NONE ": {} " NONE DIM "(at {}:{})" NONE, type,
                                       desc, file, line)) {}
};

#define COMPILER_ERROR(desc)         CompilerError(std::string_view(__FILE__), __LINE__, desc)
#define COMPILER_ERROR_T(desc, type) CompilerError(std::string_view(__FILE__), __LINE__, desc, type)
