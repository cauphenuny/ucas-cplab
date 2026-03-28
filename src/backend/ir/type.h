#pragma once

#define FMT_HEADER_ONLY
#include "fmt/format.h"
#include "utils/serialize.hpp"
#include "utils/traits.hpp"

#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>


namespace ir_type {

struct Int;
struct Float;
struct Bool;
using Primitive = std::variant<Int, Float, Bool>;

struct Sum;
struct Product;
struct Func;
struct Pointer;
struct Array;

struct Type;
using TypeBox = std::unique_ptr<Type>;

struct Int : mixin::ToBoxed<Int> {
    SIMPLE_TO_STRING("int");
};

struct Float : mixin::ToBoxed<Float> {
    SIMPLE_TO_STRING("float");
};

struct Bool : mixin::ToBoxed<Bool> {
    SIMPLE_TO_STRING("bool");
};

struct Product : mixin::ToBoxed<Product> {
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

struct Sum : mixin::ToBoxed<Sum> {
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

struct Func : mixin::ToBoxed<Func> {
    Product params;
    TypeBox ret;
    Func(Product params, TypeBox ret) : params(std::move(params)), ret(std::move(ret)) {}
    SIMPLE_TO_STRING(fmt::format("({} -> {})", params, ret))
};

struct Array : mixin::ToBoxed<Array> {
    TypeBox elem;
    size_t size;
    Array(TypeBox elem, size_t size) : elem(std::move(elem)), size(size) {}
    SIMPLE_TO_STRING(fmt::format("[{}; {}]", elem, size))
};

struct Pointer : mixin::ToBoxed<Pointer> {
    TypeBox pointee;
    Pointer(TypeBox pointee) : pointee(std::move(pointee)) {}
    SIMPLE_TO_STRING(fmt::format("{}*", pointee))
};

struct Type : mixin::ToBoxed<Type> {
    using T = std::variant<Primitive, Sum, Product, Func, Pointer, Array>;
    T type;
    Type(T type): type(std::move(type)) {}
    SIMPLE_TO_STRING(fmt::format("{}", type))
};

template <typename T> Type construct();

namespace {

template <typename T> struct false_v : std::false_type {};

template <typename T> struct is_tuple : std::false_type {};
template <typename... Args> struct is_tuple<std::tuple<Args...>> : std::true_type {};

template <typename T> struct is_variant : std::false_type {};
template <typename... Args> struct is_variant<std::variant<Args...>> : std::true_type {};

template <typename T> struct construct_func;

template <typename R, typename... Args> struct construct_func<R(Args...)> {
    static Type apply() {
        std::vector<TypeBox> items;
        (items.push_back(std::make_unique<Type>(construct<Args>())), ...);
        auto params = Product{.items = std::move(items)};
        auto ret = std::make_unique<Type>(construct<R>());
        return Type{Func(std::move(params), std::move(ret))};
    }
};

template <typename T> struct construct_product;

template <typename... Args> struct construct_product<std::tuple<Args...>> {
    static Type apply() {
        std::vector<TypeBox> items;
        (items.push_back(std::make_unique<Type>(construct<Args>())), ...);
        return Type{Product{.items = std::move(items)}};
    }
};

template <typename T> struct construct_sum;

template <typename... Args> struct construct_sum<std::variant<Args...>> {
    static Type apply() {
        std::vector<TypeBox> items;
        (items.push_back(std::make_unique<Type>(construct<Args>())), ...);
        return Type{Sum{.items = std::move(items)}};
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
template <typename T> Type construct() {
    using namespace ir_type;
    using U = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_same_v<U, int>) {
        return Type{Primitive{Int{}}};
    } else if constexpr (std::is_same_v<U, float>) {
        return Type{Primitive{Float{}}};
    } else if constexpr (std::is_same_v<U, bool>) {
        return Type{Primitive{Bool{}}};
    } else if constexpr (std::is_pointer_v<U>) {
        return Type{Pointer(std::make_unique<Type>(construct<std::remove_pointer_t<U>>()))};
    } else if constexpr (std::is_array_v<U>) {
        return Type{Array(std::make_unique<Type>(construct<std::remove_extent_t<U>>()),
                     std::extent_v<U>)};
    } else if constexpr (std::is_void_v<U>) {
        return Type{Product{}};
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

}  // namespace ir_type