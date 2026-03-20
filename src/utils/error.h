#pragma once
#include <stdexcept>

#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include "tui.h"

struct SyntaxError : std::runtime_error {
    SyntaxError(int line, int col, const std::string& desc)
        : std::runtime_error(fmt::format(RED BOLD "syntax error " NONE "at {}:{} : {}", line, col, desc)) {}
    SyntaxError(int line, int col)
        : std::runtime_error(fmt::format(RED BOLD "syntax error " NONE "at {}:{}", line, col)) {}
};