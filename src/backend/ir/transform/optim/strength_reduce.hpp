/// @brief Arithmetic Strength Reduction Pass

#pragma once
#include "../framework.hpp"
#include "backend/ir/ir.h"

#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace ir::transform {

template <typename Ctx> struct ArithmeticStrengthReduction : Pass<Ctx> {
    bool apply(Program& prog, Ctx& ctx) override {
        bool changed = false;
        for (auto& func : prog.funcs()) {
            for (auto& block : func->blocks()) {
                for (auto& inst : block->insts()) {
                    auto* binary = std::get_if<BinaryInst>(&inst);
                    if (!binary) continue;

                    auto* lhs_c = std::get_if<ConstexprValue>(&binary->lhs);
                    auto* rhs_c = std::get_if<ConstexprValue>(&binary->rhs);
                    if (!lhs_c && !rhs_c) continue;

                    // a * 2^n  →  a << n
                    if (binary->op == InstOp::MUL) {
                        auto c = lhs_c ? lhs_c : rhs_c;
                        auto x = lhs_c ? binary->rhs : binary->lhs;
                        if (auto n = log2_pow2(*c)) {
                            auto val = ConstexprValue(*n);
                            val.type = c->type;
                            block->replace(
                                &inst, BinaryInst{InstOp::SHL, binary->result, x, std::move(val)});
                            changed = true;
                            continue;
                        }
                    }
                }
            }
        }
        return changed;
    }

private:
    template <typename T> static std::optional<int> log2_pow2_impl(T v) {
        if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
            if (v <= 0) return std::nullopt;
            using U = std::make_unsigned_t<T>;
            auto uv = static_cast<U>(v);
            if ((uv & (uv - 1)) != 0) return std::nullopt;
            int n = 0;
            while (uv > 1) {
                uv >>= 1;
                n++;
            }
            return n;
        }
        return std::nullopt;
    }

    static std::optional<int> log2_pow2(const ConstexprValue& cv) {
        return match(
            cv.val, [](int v) { return log2_pow2_impl(v); },
            [](int64_t v) { return log2_pow2_impl(v); },
            [](const auto&) -> std::optional<int> { return std::nullopt; });
    }
};

}  // namespace ir::transform
