#pragma once
#define FMT_HEADER_ONLY
#include "fmt/format.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utils/serialize.hpp>

namespace ast {

enum class Type : uint8_t {
    INT,
    FLOAT,
    BOOL,
    DOUBLE,
    VOID,
};

auto toString(Type type) -> std::string {
    switch (type) {
        case Type::INT: return "int";
        case Type::FLOAT: return "float";
        case Type::BOOL: return "bool";
        case Type::DOUBLE: return "double";
        case Type::VOID: return "void";
    }
}

enum class UnaryOp : uint8_t { PLUS, MINUS, NOT };

std::string toString(UnaryOp op) {
    switch (op) {
        case UnaryOp::PLUS: return "+";
        case UnaryOp::MINUS: return "-";
        case UnaryOp::NOT: return "!";
        default: throw std::runtime_error(fmt::format("invalid unary op: {}", (uint8_t)op));
    }
}

enum class BinaryOp : uint8_t {
    MUL,
    DIV,
    MOD,  //
    ADD,
    SUB,  //
    LT,
    GT,
    LEQ,
    GEQ,  //
    EQ,
    NEQ,  //
    AND,
    OR,
};

std::string toString(BinaryOp op) {
    switch (op) {
        case BinaryOp::MUL: return "*";
        case BinaryOp::DIV: return "/";
        case BinaryOp::MOD: return "%";
        case BinaryOp::ADD: return "+";
        case BinaryOp::SUB: return "-";
        case BinaryOp::LT: return "<";
        case BinaryOp::GT: return ">";
        case BinaryOp::LEQ: return "<=";
        case BinaryOp::GEQ: return ">=";
        case BinaryOp::EQ: return "==";
        case BinaryOp::NEQ: return "!=";
        case BinaryOp::AND: return "&&";
        case BinaryOp::OR: return "||";
        default: throw std::runtime_error(fmt::format("invalid binary op: {}", (uint8_t)op));
    }
}

}  // namespace ast