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
struct Bool;
struct Float;
struct Double;
using Primitive = std::variant<Int, Float, Double, Bool>;

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

    TypeBox();
    TypeBox(TypeBox&& other) noexcept = default;
    TypeBox(const TypeBox& other);
    TypeBox& operator=(TypeBox&& other) noexcept = default;
    TypeBox& operator=(const TypeBox& other);

    [[nodiscard]] auto toString() const -> std::string;
    [[nodiscard]] auto var() const -> const Type&;
    template <typename T> [[nodiscard]] auto is() const -> bool;

    static auto match(const Type& type) -> Match<const Type&>;
    static auto match(const TypeBox& box) -> Match<const Type&>;
    template <typename T, typename... Rest>
    static auto match(T&& box, Rest&&... rest) -> decltype(auto);
};

struct Int : mixin::ToBoxed<Int, Type> {
    SIMPLE_TO_STRING("int");
};

struct Float : mixin::ToBoxed<Float, Type> {
    SIMPLE_TO_STRING("float");
};

struct Double : mixin::ToBoxed<Double, Type> {
    SIMPLE_TO_STRING("double");
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
            case 1: return fmt::format("{}", items[0]);
            default:
                std::string result;
                for (size_t i = 0; i < items.size(); i++) {
                    result += fmt::format("{}{}", items[i], i == items.size() - 1 ? "" : ", ");
                }
                return "(" + result + ")";
        }
    }
    void append(TypeBox item) {
        items.push_back(std::move(item));
    }
};

struct Sum : mixin::ToBoxed<Sum, Type> {
    std::vector<TypeBox> items;
    [[nodiscard]] std::string toString() const {
        std::string result;
        for (size_t i = 0; i < items.size(); i++) {
            result += fmt::format("{}{}", items[i], i == items.size() - 1 ? "" : " | ");
        }
        return "(" + result + ")";
    }
    void append(TypeBox item);
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
    SIMPLE_TO_STRING(size.has_value() ? fmt::format("[{}; {}]", elem, size.value())
                                      : fmt::format("[{}; *]", elem))
    [[nodiscard]] bool sized() const {
        return size.has_value();
    }
};

std::string TypeBox::toString() const {
    return serialize(*item);
}

const Type& TypeBox::var() const {
    return *item;
}

template <typename T> bool TypeBox::is() const {
    return std::holds_alternative<T>(*item);
}

auto TypeBox::match(const Type& type) -> Match<const Type&> {
    return Match{type};
}
auto TypeBox::match(const TypeBox& box) -> Match<const Type&> {
    return match(box.var());
}
template <typename T, typename... Rest>
auto TypeBox::match(T&& box, Rest&&... rest) -> decltype(auto) {
    return match(std::forward<Rest>(rest)...).with(match(std::forward<T>(box)));
}

TypeBox::TypeBox() : item(Top{}.toBoxed()) {}
TypeBox::TypeBox(const TypeBox& other) : item(std::make_unique<Type>(*other.item)) {}
TypeBox& TypeBox::operator=(const TypeBox& other) {
    if (this != &other) {
        item = std::make_unique<Type>(*other.item);
    }
    return *this;
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
 * - T* -> Slice(unsized)
 * - T[N] -> Slice(sized)
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
    } else if constexpr (std::is_same_v<U, double>) {
        return Double{}.toBoxed();
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

inline bool operator<=(const TypeBox&, const Type&);
inline bool operator<=(const Type&, const TypeBox&);
inline bool operator<=(const TypeBox&, const TypeBox&);
inline bool operator<=(const Type&, const Type&);

template <typename T, typename = std::enable_if_t<!std::is_same_v<T, Type>>>
bool operator<=(const T& from, const TypeBox& to);
template <typename T, typename = std::enable_if_t<!std::is_same_v<T, Type>>>
bool operator<=(const TypeBox& from, const T& to);

template <typename T1, typename T2> bool operator<=(const T1& from, const T2& to) {
    if constexpr (std::is_same_v<T1, Bottom>) return true;
    if constexpr (std::is_same_v<T2, Top>) return true;
    return false;
}

template <typename T, typename = std::enable_if_t<std::disjunction_v<
                          std::is_same<T, Int>, std::is_same<T, Float>, std::is_same<T, Bool>,
                          std::is_same<T, Double>, std::is_same<T, Top>, std::is_same<T, Bottom>>>>
bool operator<=(const T& from, const T& to) {
    return true;
}

template <typename T, typename = std::enable_if_t<
                          std::disjunction_v<std::is_same<T, Int>, std::is_same<T, Float>,
                                             std::is_same<T, Bool>, std::is_same<T, Double>>>>
bool operator<=(const Primitive& from, const T& to) {
    return Match{from}([&](const auto& from) -> bool { return from <= to; });
}

template <typename T, typename = std::enable_if_t<
                          std::disjunction_v<std::is_same<T, Int>, std::is_same<T, Float>,
                                             std::is_same<T, Bool>, std::is_same<T, Double>>>>
bool operator<=(const T& from, const Primitive& to) {
    return Match{to}([&](const auto& to) -> bool { return from <= to; });
}

bool operator<=(const Primitive& from, const Primitive& to) {
    return Match{from, to}([](const auto& from, const auto& to) -> bool { return from <= to; });
}

template <typename ArrayLike, typename T,
          std::enable_if_t<
              std::disjunction_v<std::is_same<ArrayLike, Product>, std::is_same<ArrayLike, Sum>>>>
inline bool operator<=(const ArrayLike& from, const T& to) {
    if (from.items.size() == 1) {
        return from.items[0] <= to;
    } else {
        return false;
    }
}

template <typename ArrayLike, typename T,
          std::enable_if_t<
              std::disjunction_v<std::is_same<ArrayLike, Product>, std::is_same<ArrayLike, Sum>>>>
inline bool operator<=(const T& from, const ArrayLike& to) {
    if (to.items.size() == 1) {
        return from <= to.items[0];
    } else {
        return false;
    }
}

inline bool operator<=(const Product& from, const Product& to) {
    if (from.items.size() != to.items.size()) {
        return false;
    }
    for (size_t i = 0; i < from.items.size(); i++) {
        if (!(from.items[i] <= to.items[i])) {
            return false;
        }
    }
    return true;
}

template <typename T, typename = std::enable_if_t<
                          !std::disjunction_v<std::is_same<T, TypeBox>, std::is_same<T, Type>>>>
bool operator<=(const Sum& from, const T& to) {
    for (const auto& item : from.items) {
        if (!(item <= to)) return false;
    }
    return true;
}

template <typename T, typename = std::enable_if_t<
                          !std::disjunction_v<std::is_same<T, TypeBox>, std::is_same<T, Type>>>>
bool operator<=(const T& from, const Sum& to) {
    for (const auto& item : to.items) {
        if (from <= item) return true;
    }
    return false;
}

bool operator<=(const Sum& from, const Sum& to) {  // forall T in from s.t. T -> to
    for (const auto& item : from.items) {
        if (!(item <= to)) return false;
    }
    return true;
}

bool operator<=(const Func& from, const Func& to) {
    return (to.params <= from.params) &&  // contravariance
           (from.ret <= to.ret);          // covariance
}

bool operator<=(const Slice& from, const Slice& to) {
    if (!(from.elem <= to.elem)) {
        return false;
    }
    if (!to.sized()) return true;  // NOTE: for C-like lang, array is convertible to pointer
    if (!from.sized()) return !to.sized();
    return from.size.value() >= to.size.value();
}

inline bool operator<=(const TypeBox& from, const TypeBox& to) {
    return operator<=(from.var(), to.var());
}

template <typename T, typename> bool operator<=(const T& from, const TypeBox& to) {
    return operator<=(from, to.var());
}

template <typename T, typename> bool operator<=(const TypeBox& from, const T& to) {
    return operator<=(from.var(), to);
}

template <typename T, typename = std::enable_if_t<!std::is_same_v<T, Type>>>
bool operator<=(const T& from, const Type& to) {
    return Match(to)([&](const auto& to) -> bool {
        using To = std::decay_t<decltype(to)>;
        if constexpr (std::is_same_v<To, Top>) {
            return true;
        }
        return from <= to;
    });
}

template <typename T, typename = std::enable_if_t<!std::is_same_v<T, Type>>>
bool operator<=(const Type& from, const T& to) {
    return Match(from)([&](const auto& from) -> bool {
        using From = std::decay_t<decltype(from)>;
        if constexpr (std::is_same_v<From, Bottom>) {
            return true;
        }
        return from <= to;
    });
}

inline bool operator<=(const Type& from, const Type& to) {
    return Match(from)([&](const auto& from) -> bool {
        using From = std::decay_t<decltype(from)>;
        if constexpr (std::is_same_v<From, Bottom>) {
            return true;
        }
        return from <= to;
    });
}

template <typename T1, typename T2> bool operator==(const T1& from, const T2& to) {
    return (from <= to) && (to <= from);
}

void Sum::append(TypeBox item) {
    for (const auto& i : items) {
        if (i == item) return;
    }
    items.push_back(std::move(item));
}

TypeBox operator|(const TypeBox& lhs, const TypeBox& rhs) {
    return TypeBox::match(lhs, rhs)(
        [](const Sum& lhs, const Sum& rhs) {
            Sum result = lhs;
            for (const auto& item : rhs.items) {
                result.append(item);
            }
            return std::move(result).toBoxed();
        },
        [&](const Sum& lhs, const auto&) {
            Sum result = lhs;
            result.append(rhs);
            return std::move(result).toBoxed();
        },
        [&](const auto&, const Sum& rhs) {
            Sum result;
            result.append(lhs);
            for (const auto& item : rhs.items) {
                result.append(item);
            }
            return std::move(result).toBoxed();
        },
        [&](const auto&, const auto&) {
            Sum result;
            result.append(lhs);
            result.append(rhs);
            return std::move(result).toBoxed();
        });
}

}  // namespace adt