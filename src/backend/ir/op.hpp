#pragma once
#include "frontend/ast/op.hpp"
#include "utils/diagnosis.hpp"

#include <cstdint>
#include <string>

namespace ir {

enum class UnaryInstOp : uint8_t { MOV, NOT, NEG, LOAD, BORROW, BORROW_MUT };

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
    OR,
    STORE,
    LOAD_ELEM,
    BORROW_ELEM,
    BORROW_ELEM_MUT,
};

inline std::string toString(UnaryInstOp op) {
    switch (op) {
        case UnaryInstOp::MOV: return "";
        case UnaryInstOp::NOT: return "! ";
        case UnaryInstOp::NEG: return "- ";
        case UnaryInstOp::LOAD: return "* ";
        case UnaryInstOp::BORROW: return "& ";
        case UnaryInstOp::BORROW_MUT: return "&mut ";
    }
}

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
        case InstOp::STORE: return "store";
        case InstOp::LOAD_ELEM: return "load";
        case InstOp::BORROW_ELEM: return "& ";
        case InstOp::BORROW_ELEM_MUT: return "&mut ";
    }
}

inline auto convert_op(ast::BinaryOp op) -> InstOp {
    switch (op) {
        case ast::BinaryOp::MUL: return InstOp::MUL;
        case ast::BinaryOp::DIV: return InstOp::DIV;
        case ast::BinaryOp::MOD: return InstOp::MOD;
        case ast::BinaryOp::ADD: return InstOp::ADD;
        case ast::BinaryOp::SUB: return InstOp::SUB;
        case ast::BinaryOp::LT: return InstOp::LT;
        case ast::BinaryOp::GT: return InstOp::GT;
        case ast::BinaryOp::LEQ: return InstOp::LEQ;
        case ast::BinaryOp::GEQ: return InstOp::GEQ;
        case ast::BinaryOp::EQ: return InstOp::EQ;
        case ast::BinaryOp::NEQ: return InstOp::NEQ;
        case ast::BinaryOp::AND: return InstOp::AND;
        case ast::BinaryOp::OR: return InstOp::OR;
        default: throw COMPILER_ERROR(fmt::format("unsupported binary operator '{}'", op));
    }
}

inline bool commutative(InstOp op) {
    return op == InstOp::MUL || op == InstOp::ADD || op == InstOp::AND || op == InstOp::OR ||
           op == InstOp::EQ || op == InstOp::NEQ;
}

inline auto convert_op(ast::UnaryOp op) -> UnaryInstOp {
    switch (op) {
        case ast::UnaryOp::PLUS: return UnaryInstOp::MOV;
        case ast::UnaryOp::MINUS: return UnaryInstOp::NEG;
        case ast::UnaryOp::NOT: return UnaryInstOp::NOT;
    }
}

}  // namespace ir