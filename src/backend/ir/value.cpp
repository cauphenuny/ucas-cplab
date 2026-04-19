#include "backend/ir/ir.h"
#include "op.hpp"
#include "type.hpp"
#include "utils/match.hpp"

#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>

namespace ir {

auto NamedValue::toString() const -> std::string {
    return match(def, [&](const auto* def) { return def->name; });
}

auto toString(int val) -> std::string {
    return fmt::format("{}", val);
}
auto toString(float val) -> std::string {
    return fmt::format("{:#g}f", val);
}
auto toString(double val) -> std::string {
    return fmt::format("{:#g}", val);
}
auto toString(bool val) -> std::string {
    return val ? "true" : "false";
}

auto serializeArray(const Type& type, std::byte* buffer) -> std::string {
    std::string result;
    auto flat_type = type.flatten().as<ir::type::Array>();
    auto elem_type = flat_type.elem;
    if (!elem_type.is<ir::type::Primitive>()) {
        throw COMPILER_ERROR(
            fmt::format("Unsupported type in ConstexprValue array: {}", elem_type));
    }
    auto prim = elem_type.as<ir::type::Primitive>();
    size_t size = flat_type.size, len = 1;
    auto check_ptr = buffer;
    for (size_t i = 0; i < size; i++) {
        if (match(prim, [&](auto v) {
                using type = typename decltype(v)::type;
                return (*(type*)check_ptr) != 0;
            })) {
            len = i + 1;
        }
        check_ptr += ir::type::size_of(prim);
    }
    for (size_t i = 0; i < len; i++) {
        result += match(prim,
                        [&](auto v) {
                            using type = typename decltype(v)::type;
                            return toString(*(type*)buffer);
                        }) +
                  ", ";
        buffer += ir::type::size_of(prim);
    }
    if (result.size()) result.pop_back(), result.pop_back();
    return "{" + result + "}";
}

ConstexprValue::ConstexprValue(const ConstexprValue& other) : type(other.type) {
    match(
        other.val, [&](std::monostate) { val = std::monostate{}; },
        [&](const std::unique_ptr<std::byte[]>& vec) {
            if (vec) {
                auto size = ir::type::size_of(type);
                auto buf = std::make_unique<std::byte[]>(size);
                std::memcpy(buf.get(), vec.get(), size);
                val = std::move(buf);
            } else {
                val = std::unique_ptr<std::byte[]>{};
            }
        },
        [&](auto v) { val = v; });
}

ConstexprValue& ConstexprValue::operator=(const ConstexprValue& other) {
    if (this != &other) {
        *this = ConstexprValue(other);
    }
    return *this;
}

ConstexprValue ConstexprValue::zeros_like(const Type& type) {
    if (type.is<type::Primitive>()) {
        return match(type.as<type::Primitive>(),
                     [](auto v) { return ConstexprValue(typename decltype(v)::type(0)); });
    } else if (type.is<type::Array>()) {
        auto size = ir::type::size_of(type);
        auto buf = std::make_unique<std::byte[]>(size);
        std::memset(buf.get(), 0, size);
        return {type.flatten(), std::move(buf)};
    } else {
        throw COMPILER_ERROR(fmt::format("Unsupported type in zeros_like: {}", type));
    }
}

bool operator==(const ConstexprValue& lhs, const ConstexprValue& rhs) {
    if (!(lhs.type == rhs.type)) return false;
    if (lhs.type.is<ir::type::Array>()) {
        auto array_type = lhs.type.flatten().as<ir::type::Array>();
        auto elem_type = array_type.elem;
        auto size = array_type.size;
        auto prim_type = elem_type.as<ir::type::Primitive>();
        return Match{prim_type}([&](auto p) {
            using T = typename decltype(p)::type;
            auto lhs_array = (const T*)std::get<std::unique_ptr<std::byte[]>>(lhs.val).get();
            auto rhs_array = (const T*)std::get<std::unique_ptr<std::byte[]>>(rhs.val).get();
            for (size_t i = 0; i < size; i++) {
                if (lhs_array[i] != rhs_array[i]) return false;
            }
            return true;
        });
    } else {
        return lhs.val == rhs.val;
    }
}

auto SSAValue::toString() const -> std::string {
    return fmt::format("${}.{}", def->name, version);
}

LeftValue as_lvalue(const Value& value) {
    return match(
        value, [&](const LeftValue& val) -> LeftValue { return val; },
        [&](const ConstexprValue& c) -> LeftValue {
            throw COMPILER_ERROR(fmt::format("expected LeftValue, got {}", c));
        });
}

auto type_of(const LeftValue& value) -> Type {
    return match(value, [&](const auto& var) { return var.type; });
}

auto type_of(const ConstexprValue& value) -> Type {
    return value.type;
}

auto type_of(const Value& value) -> Type {
    return match(value, [&](const auto& val) { return type_of(val); });
}

}  // namespace ir

size_t std::hash<ir::ConstexprValue>::hash_array(const ir::Type& elem_type, std::byte* buffer,
                                                 size_t length) const {
    auto prim = elem_type.as<ir::type::Primitive>();
    return Match{prim}([&](auto p) {
        using T = typename decltype(p)::type;
        auto array = (T*)buffer;
        while (length > 0 && array[length - 1] == T{}) {
            --length;
        }
        size_t h = 0;
        for (size_t i = 0; i < length; ++i) {
            h ^= std::hash<T>{}(array[i]) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    });
}

size_t std::hash<ir::ConstexprValue>::operator()(const ir::ConstexprValue& v) const noexcept {
    return Match{v.val}(
        [&](const std::unique_ptr<std::byte[]>& buffer) {
            auto array_type = v.type.flatten().as<ir::type::Array>();
            return hash_array(array_type.elem, buffer.get(), array_type.size);
        },
        [&](const auto&) {
            return std::hash<std::variant<std::monostate, int, float, bool, double,
                                          std::unique_ptr<std::byte[]>>>{}(v.val);
        });
}
