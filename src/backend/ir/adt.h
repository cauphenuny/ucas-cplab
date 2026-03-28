/// @brief algebraic data types for IR

#pragma once

#define FMT_HEADER_ONLY
#include "fmt/format.h"
#include "utils/serialize.hpp"
#include "utils/traits.hpp"

#include <any>
#include <cstddef>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace adt {

struct Int;
struct Float;
struct Bool;
using Primitive = std::variant<Int, Float, Bool>;

struct Func;
struct Slice;

struct Sum;
struct Product;
struct Top;
struct Bottom;

using Type = std::variant<Primitive, Sum, Product, Func, Slice, Top, Bottom>;

struct TypeBox {
    using T = std::unique_ptr<Type>;
    T item;
    TypeBox(T item) : item(std::move(item)) {}
    [[nodiscard]] auto toString() const -> std::string;
    [[nodiscard]] auto var() const -> const Type&;
};

struct Int : mixin::ToBoxed<Int, Type> {
    SIMPLE_TO_STRING("int");
};

struct Float : mixin::ToBoxed<Float, Type> {
    SIMPLE_TO_STRING("float");
};

struct Bool : mixin::ToBoxed<Bool, Type> {
    SIMPLE_TO_STRING("bool");
};

struct Bottom : mixin::ToBoxed<Bottom, Type> {
    SIMPLE_TO_STRING("⊥");
};

struct Top : mixin::ToBoxed<Top, Type> {
    SIMPLE_TO_STRING("⊤");
};

struct Product : mixin::ToBoxed<Product, Type> {
    std::vector<TypeBox> items;
    [[nodiscard]] std::string toString() const {
        switch (items.size()) {
            case 0: return "()";
            case 1: return fmt::format("({},)", items[0]);
            default:
                std::string result;
                for (size_t i = 0; i < items.size(); i++) {
                    result += fmt::format("{}{}", items[i], i == items.size() - 1 ? "" : ", ");
                }
                return "(" + result + ")";
        }
    }
};

struct Sum : mixin::ToBoxed<Sum, Type> {
    std::vector<TypeBox> items;
    [[nodiscard]] std::string toString() const {
        switch (items.size()) {
            case 0: return "()";
            case 1: return fmt::format("({} |)", items[0]);
            default:
                std::string result;
                for (size_t i = 0; i < items.size(); i++) {
                    result += fmt::format("{}{}", items[i], i == items.size() - 1 ? "" : " | ");
                }
                return "(" + result + ")";
        }
    }
};

struct Func : mixin::ToBoxed<Func, Type> {
    Product params;
    TypeBox ret;
    Func(Product params, TypeBox ret) : params(std::move(params)), ret(std::move(ret)) {}
    SIMPLE_TO_STRING(fmt::format("({} -> {})", params, ret))
};

struct Slice : mixin::ToBoxed<Slice, Type> {
    TypeBox elem;
    std::optional<size_t> size;
    Slice(TypeBox elem, std::optional<size_t> size) : elem(std::move(elem)), size(size) {}
    Slice(TypeBox elem) : elem(std::move(elem)), size(std::nullopt) {}
    SIMPLE_TO_STRING(size.has_value() ? fmt::format("[{}; {}]", elem, size.value()) : fmt::format("[{}; *]", elem))
    [[nodiscard]] bool sized() const { return size.has_value(); }
};

std::string TypeBox::toString() const {
    return serialize(*item);
}

const Type& TypeBox::var() const {
    return *item;
}

/************************************************************/

template <typename T> TypeBox construct();

namespace {

template <typename T> struct false_v : std::false_type {};

template <typename T> struct is_tuple : std::false_type {};
template <typename... Args> struct is_tuple<std::tuple<Args...>> : std::true_type {};

template <typename T> struct is_variant : std::false_type {};
template <typename... Args> struct is_variant<std::variant<Args...>> : std::true_type {};

template <typename T> struct construct_func;

template <typename R, typename... Args> struct construct_func<R(Args...)> {
    static TypeBox apply() {
        std::vector<TypeBox> items;
        (items.push_back(construct<Args>()), ...);
        auto params = Product{.items = std::move(items)};
        auto ret = construct<R>();
        return Func(std::move(params), std::move(ret)).toBoxed();
    }
};

template <typename T> struct construct_product;

template <typename... Args> struct construct_product<std::tuple<Args...>> {
    static TypeBox apply() {
        std::vector<TypeBox> items;
        (items.push_back(construct<Args>()), ...);
        return Product{.items = std::move(items)}.toBoxed();
    }
};

template <typename T> struct construct_sum;

template <typename... Args> struct construct_sum<std::variant<Args...>> {
    static TypeBox apply() {
        std::vector<TypeBox> items;
        (items.push_back(construct<Args>()), ...);
        return Sum{.items = std::move(items)}.toBoxed();
    }
};

}  // namespace

/**
 * @brief Construct an IR Type from a C++ type T.
 *
 * Supported mappings:
 * - int -> Int
 * - float -> Float
 * - bool -> Bool
 * - T* -> Pointer
 * - T[N] -> Array
 * - void -> Product (empty)
 * - R(Args...) -> Func
 * - std::tuple<Args...> -> Product
 * - std::variant<Args...> -> Sum
 */
template <typename T> TypeBox construct() {
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<U, int>) {
        return Int{}.toBoxed();
    } else if constexpr (std::is_same_v<U, float>) {
        return Float{}.toBoxed();
    } else if constexpr (std::is_same_v<U, bool>) {
        return Bool{}.toBoxed();
    } else if constexpr (std::is_same_v<U, std::any>) {
        return Top{}.toBoxed();
    } else if constexpr (std::is_pointer_v<U>) {
        return Slice(construct<std::remove_pointer_t<U>>()).toBoxed();
    } else if constexpr (std::is_array_v<U>) {
        return Slice(construct<std::remove_extent_t<U>>(), std::extent_v<U>).toBoxed();
    } else if constexpr (std::is_void_v<U>) {
        return Product{}.toBoxed();
    } else if constexpr (std::is_function_v<U>) {
        return construct_func<U>::apply();
    } else if constexpr (is_tuple<U>::value) {
        return construct_product<U>::apply();
    } else if constexpr (is_variant<U>::value) {
        return construct_sum<U>::apply();
    } else {
        static_assert(false_v<U>::value, "Unsupported type for ir::construct<T>");
    }
}

/************************************************************/

inline bool isSubtype(const Type&, const Type&);
inline bool isSubtype(const TypeBox&, const TypeBox&);
inline bool isSubtype(const TypeBox&, const Type&);
inline bool isSubtype(const Type&, const TypeBox&);

inline bool isSubtype(const Product& from, const Product& to) {
    if (from.items.size() != to.items.size()) return false;
    for (size_t i = 0; i < from.items.size(); i++) {
        if (!isSubtype(from.items[i].var(), to.items[i].var())) return false;
    }
    return true;
}

template <typename T>
bool isSubtype(const T& from, const Sum& to) {  // exists T in to s.t. from -> T
    for (const auto& item : to.items) {
        if (isSubtype(from, item.var())) return true;
    }
    return false;
}

inline bool isSubtype(const Type& from, const Type& to) {
    return Match(from, to)(
        [](const Primitive& from, const Primitive& to) -> bool {
            return Match{from, to}([](const auto& from, const auto& to) -> bool {
                return std::is_same_v<decltype(from), decltype(to)>;
            });
        },
        [](const Product& from, const Product& to) -> bool { return isSubtype(from, to); },
        [&](const Sum& from, const auto& /*to*/) -> bool {
            for (const auto& item : from.items) {
                if (!isSubtype(item, to)) return false;
            }
            return true;
        },
        [&](const auto& /*from*/, const Sum& to) -> bool { return isSubtype(from, to); },
        [](const Sum& from, const Sum& to) -> bool {
            for (const auto& f : from.items) {
                if (!isSubtype(f, to)) return false;
            }
            return true;
        },
        [](const Func& from, const Func& to) -> bool {
            return isSubtype(to.params, from.params) &&  // contravariance
                   isSubtype(from.ret, to.ret);          // covariance
        },
        [](const Slice& from, const Slice& to) -> bool {
            if (!isSubtype(from.elem, to.elem)) {
                return false;
            }
            if (!to.sized()) return true; // NOTE: for C-like lang, array is convertible to pointer
            if (!from.sized()) return !to.sized();
            return from.size.value() >= to.size.value();
        },
        [](const auto& from, const auto& to) -> bool {
            if constexpr (std::is_same_v<std::decay_t<decltype(from)>, Bottom> ||
                          std::is_same_v<std::decay_t<decltype(to)>, Top>) {
                return true;
            }
            return false;
        });
}

inline bool isSubtype(const TypeBox& from, const TypeBox& to) {
    return isSubtype(from.var(), to.var());
}
inline bool isSubtype(const TypeBox& from, const Type& to) {
    return isSubtype(from.var(), to);
}
inline bool isSubtype(const Type& from, const TypeBox& to) {
    return isSubtype(from, to.var());
}

}  // namespace adt