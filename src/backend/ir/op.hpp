#pragma once
#define FMT_HEADER_ONLY
#include "fmt/format.h"

#include <cstdint>
#include <string>

namespace ir {

enum class InstOp : uint8_t {
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
    OR,     //
    LOAD,   //
    STORE,  //
    CALL, //
    MOV, //
};

inline std::string toString(InstOp op) {
    switch (op) {
        case InstOp::MUL: return "*";
        case InstOp::DIV: return "/";
        case InstOp::MOD: return "%";
        case InstOp::ADD: return "+";
        case InstOp::SUB: return "-";
        case InstOp::LT: return "<";
        case InstOp::GT: return ">";
        case InstOp::LEQ: return "<=";
        case InstOp::GEQ: return ">=";
        case InstOp::EQ: return "==";
        case InstOp::NEQ: return "!=";
        case InstOp::AND: return "&&";
        case InstOp::OR: return "||";
        case InstOp::LOAD: return "ld";
        case InstOp::STORE: return "st";
        case InstOp::CALL: return "call";
        default: throw std::runtime_error(fmt::format("invalid inst op: {}", (uint8_t)op));
    }
}

}  // namespace ir