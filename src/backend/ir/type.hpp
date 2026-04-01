/// @brief algebraic data types for IR

#pragma once

#include "utils/error.hpp"
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
struct Array;
struct Pointer;

struct Sum;
struct Product;
struct Top;
struct Bottom;

using Type = std::variant<Primitive, Sum, Product, Func, Array, Pointer, Top, Bottom>;

struct TypeBox {
    std::unique_ptr<Type> item;
    TypeBox(std::unique_ptr<Type> item) : item(std::move(item)) {}

    TypeBox();
    TypeBox(TypeBox&& other) noexcept = default;
    TypeBox(const TypeBox& other);
    TypeBox& operator=(TypeBox&& other) noexcept = default;
    TypeBox& operator=(const TypeBox& other);

    [[nodiscard]] auto toString() const -> std::string;
    [[nodiscard]] auto var() const -> const Type&;
    [[nodiscard]] auto immutable() const -> bool;
    template <typename T> [[nodiscard]] auto is() const -> bool;
    template <typename T> [[nodiscard]] auto as() const -> const T&;

    static auto match(const Type& type) -> Match<const Type&>;
    static auto match(const TypeBox& box) -> Match<const Type&>;
    template <typename T, typename... Rest>
    static auto match(T&& box, Rest&&... rest) -> decltype(auto);

    [[nodiscard]] auto decay() const -> TypeBox;
    [[nodiscard]] auto flatten() const -> TypeBox;
};

struct Int : mixin::ToBoxed<Int, Type> {
    using type = int;
    bool immutable{false};
    SIMPLE_TO_STRING(immutable ? "int const" : "int");
};

struct Float : mixin::ToBoxed<Float, Type> {
    using type = float;
    bool immutable{false};
    SIMPLE_TO_STRING(immutable ? "float const" : "float");
};

struct Double : mixin::ToBoxed<Double, Type> {
    using type = double;
    bool immutable{false};
    SIMPLE_TO_STRING(immutable ? "double const" : "double");
};

struct Bool : mixin::ToBoxed<Bool, Type> {
    using type = bool;
    bool immutable{false};
    SIMPLE_TO_STRING(immutable ? "bool const" : "bool");
};

struct Bottom : mixin::ToBoxed<Bottom, Type> {
    inline constexpr static bool immutable{true};
    SIMPLE_TO_STRING("⊥");
};

struct Top : mixin::ToBoxed<Top, Type> {
    inline constexpr static bool immutable{true};
    SIMPLE_TO_STRING("⊤");
};

struct Product : mixin::ToBoxed<Product, Type> {
    bool immutable{false};
    [[nodiscard]] std::string toString() const {
        switch (items_.size()) {
            case 0: return "()";
            case 1: return fmt::format("({},)", items_[0]);
            default:
                std::string result;
                for (size_t i = 0; i < items_.size(); i++) {
                    result += fmt::format("{}{}", items_[i], i == items_.size() - 1 ? "" : ", ");
                }
                return fmt::format("({}){}", result, immutable ? " const" : "");
        }
    }
    void append(TypeBox item);
    friend bool operator<=(const Product& from, const Product& to);
    [[nodiscard]] const auto& items() const {
        return items_;
    }

private:
    std::vector<TypeBox> items_;
};

struct Sum : mixin::ToBoxed<Sum, Type> {
    bool immutable{false};
    [[nodiscard]] std::string toString() const {
        std::string result;
        for (size_t i = 0; i < items_.size(); i++) {
            result += fmt::format("{}{}", items_[i], i == items_.size() - 1 ? "" : " | ");
        }
        return fmt::format("({}){}", result, immutable ? " const" : "");
    }
    void append(TypeBox item);
    [[nodiscard]] const auto& items() const {
        return items_;
    }

    friend TypeBox operator|(const TypeBox& lhs, const TypeBox& rhs);
    friend bool operator<=(const Sum& from, const Sum& to);
    template <typename T>
    friend std::enable_if_t<!std::disjunction_v<std::is_same<T, TypeBox>, std::is_same<T, Type>>,
                            bool>
    operator<=(const Sum& from, const T& to);
    template <typename T>
    friend std::enable_if_t<!std::disjunction_v<std::is_same<T, TypeBox>, std::is_same<T, Type>>,
                            bool>
    operator<=(const T& from, const Sum& to);

    Sum(std::vector<TypeBox> items) {
        for (auto& item : items) {
            append(std::move(item));
        }
        if (items_.size() < 2) {
            throw CompilerError("Sum type must have at least 2 items");
        }
    }

    [[nodiscard]] size_t index_of(const TypeBox& item) const;
    [[nodiscard]] const TypeBox& type_of(size_t index) const {
        if (index >= items_.size()) {
            throw CompilerError(fmt::format("Index {} out of bounds for Sum type {}", index, *this));
        }
        return items_[index];
    }

private:
    std::vector<TypeBox> items_;
};

struct Func : mixin::ToBoxed<Func, Type> {
    bool immutable{false};
    Product params;
    TypeBox ret;
    Func(Product params, TypeBox ret) : params(std::move(params)), ret(std::move(ret)) {}
    SIMPLE_TO_STRING(fmt::format("{}{} -> {}{}", immutable ? "(" : "", params, ret,
                                 immutable ? ") const" : ""))
};

/// Unsized array / pointer type: [elem; *]
struct Pointer : mixin::ToBoxed<Pointer, Type> {
    bool immutable{false};
    TypeBox elem;
    Pointer(TypeBox elem) : elem(std::move(elem)) {}
    SIMPLE_TO_STRING(fmt::format("[{}; *]{}", elem, immutable ? " const" : ""));
};

/// Sized array type: [elem; size]
struct Array : mixin::ToBoxed<Array, Type> {
    bool immutable{false};
    TypeBox elem;
    size_t size;
    Array(TypeBox elem, size_t size) : elem(std::move(elem)), size(size) {}
    SIMPLE_TO_STRING(fmt::format("[{}; {}]{}", elem, size, immutable ? " const" : ""));
    [[nodiscard]] auto flatten() const -> Array;
    [[nodiscard]] auto decay() const -> Pointer;
};

inline std::string TypeBox::toString() const {
    return serialize(*item);
}

inline const Type& TypeBox::var() const {
    return *item;
}

inline bool TypeBox::immutable() const {
    return Match{*item}([](const auto& t) -> bool {
        using T = std::decay_t<decltype(t)>;
        if constexpr (std::is_same_v<T, Product> || std::is_same_v<T, Sum> ||
                      std::is_same_v<T, Func> || std::is_same_v<T, Array> ||
                      std::is_same_v<T, Pointer>) {
            return t.immutable;
        } else if constexpr (std::is_same_v<T, Primitive>) {
            return Match{t}([](const auto& prim) -> bool { return prim.immutable; });
        } else {
            return true;  // Top and Bottom are always immutable
        }
    });
}

template <typename T> bool TypeBox::is() const {
    return std::holds_alternative<T>(*item);
}

template <typename T> const T& TypeBox::as() const {
    if (!is<T>()) {
        throw std::bad_variant_access();
    }
    return std::get<T>(*item);
}

inline auto TypeBox::match(const Type& type) -> Match<const Type&> {
    return Match{type};
}
inline auto TypeBox::match(const TypeBox& box) -> Match<const Type&> {
    return match(box.var());
}
template <typename T, typename... Rest>
auto TypeBox::match(T&& box, Rest&&... rest) -> decltype(auto) {
    return match(std::forward<Rest>(rest)...).with(match(std::forward<T>(box)));
}

inline TypeBox::TypeBox() : item(Top{}.toBoxed()) {}
inline TypeBox::TypeBox(const TypeBox& other) : item(std::make_unique<Type>(*other.item)) {}
inline TypeBox& TypeBox::operator=(const TypeBox& other) {
    if (this != &other) {
        item = std::make_unique<Type>(*other.item);
    }
    return *this;
}
inline void Product::append(TypeBox item) {
    items_.push_back(std::move(item));
}

inline auto Array::flatten() const -> Array {
    if (elem.is<Array>()) {
        const auto& inner = elem.as<Array>();
        size_t new_size = size * inner.size;
        return Array(inner.elem, new_size).flatten();
    }
    return *this;
}

inline auto Array::decay() const -> Pointer {
    auto p = Pointer(elem);
    p.immutable = immutable;
    return p;
}

inline auto TypeBox::decay() const -> TypeBox {
    return Match{*item}([&](const Array& arr) -> TypeBox { return arr.decay().toBoxed(); },
                        [&](const auto&) { return *this; });
}

inline auto TypeBox::flatten() const -> TypeBox {
    return Match{*item}([&](const Array& arr) -> TypeBox { return arr.flatten().toBoxed(); },
                        [&](const auto&) { return *this; });
}

/************************************************************/

template <typename T> TypeBox construct();

namespace {

template <typename T> struct false_v : std::false_type {};

template <typename T> struct is_tuple : std::false_type {};
template <typename... Args> struct is_tuple<std::tuple<Args...>> : std::true_type {};

template <typename T> struct is_variant : std::false_type {};
template <typename... Args> struct is_variant<std::variant<Args...>> : std::true_type {};

template <typename T> T mark_immutable(bool immutable) {
    auto t = T{};
    t.immutable = immutable;
    return t;
}

template <typename T> struct construct_func;

template <typename R, typename... Args> struct construct_func<R(Args...)> {
    static TypeBox apply(bool immutable = false) {
        auto params = Product{};
        (params.append(construct<Args>()), ...);
        auto ret = construct<R>();
        auto func = Func(std::move(params), std::move(ret));
        func.immutable = immutable;
        return std::move(func).toBoxed();
    }
};

template <typename T> struct construct_product;

template <typename... Args> struct construct_product<std::tuple<Args...>> {
    static TypeBox apply(bool immutable = false) {
        auto params = Product{};
        (params.append(construct<Args>()), ...);
        params.immutable = immutable;
        return std::move(params).toBoxed();
    }
};

template <typename T> struct construct_sum;

template <typename... Args> struct construct_sum<std::variant<Args...>> {
    static TypeBox apply(bool immutable = false) {
        auto items = std::vector<TypeBox>{construct<Args>()...};
        auto sum = Sum(std::move(items));
        sum.immutable = immutable;
        return std::move(sum).toBoxed();
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
    using Raw = std::remove_reference_t<T>;
    using U = std::remove_cv_t<Raw>;
    constexpr bool immutable = std::is_const_v<Raw>;

    if constexpr (std::is_same_v<U, int>) {
        return mark_immutable<Int>(immutable).toBoxed();
    } else if constexpr (std::is_same_v<U, float>) {
        return mark_immutable<Float>(immutable).toBoxed();
    } else if constexpr (std::is_same_v<U, double>) {
        return mark_immutable<Double>(immutable).toBoxed();
    } else if constexpr (std::is_same_v<U, bool>) {
        return mark_immutable<Bool>(immutable).toBoxed();
    } else if constexpr (std::is_same_v<U, std::any>) {
        return Top{}.toBoxed();
    } else if constexpr (std::is_pointer_v<U>) {
        auto t = Pointer(construct<std::remove_pointer_t<Raw>>());
        t.immutable = immutable;
        return std::move(t).toBoxed();
    } else if constexpr (std::is_array_v<U>) {
        auto t = Array(construct<std::remove_extent_t<Raw>>(), std::extent_v<U>);
        t.immutable = immutable;
        return std::move(t).toBoxed();
    } else if constexpr (std::is_void_v<U>) {
        auto t = Product{};
        t.immutable = immutable;
        return std::move(t).toBoxed();
    } else if constexpr (std::is_function_v<U>) {
        return construct_func<U>::apply(immutable);
    } else if constexpr (is_tuple<U>::value) {
        return construct_product<U>::apply(immutable);
    } else if constexpr (is_variant<U>::value) {
        return construct_sum<U>::apply(immutable);
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
    return !to.immutable || from.immutable;  // if to is immutable, from must also be immutable
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

inline bool operator<=(const Primitive& from, const Primitive& to) {
    return Match{from, to}([](const auto& from, const auto& to) -> bool { return from <= to; });
}

inline bool operator<=(const Product& from, const Product& to) {
    if (from.items_.size() != to.items_.size()) {
        return false;
    }
    for (size_t i = 0; i < from.items_.size(); i++) {
        if (!(from.items_[i] <= to.items_[i])) {
            return false;
        }
    }
    return true;
}

template <typename T>
std::enable_if_t<!std::disjunction_v<std::is_same<T, TypeBox>, std::is_same<T, Type>>, bool>
operator<=(const Sum& from, const T& to) {
    for (const auto& item : from.items_) {
        if (!(item <= to)) return false;
    }
    return true;
}

template <typename T>
std::enable_if_t<!std::disjunction_v<std::is_same<T, TypeBox>, std::is_same<T, Type>>, bool>
operator<=(const T& from, const Sum& to) {
    for (const auto& item : to.items_) {
        if (from <= item) return true;
    }
    return false;
}

inline bool operator<=(const Sum& from, const Sum& to) {  // forall T in from s.t. T -> to
    for (const auto& item : from.items_) {
        if (!(item <= to)) return false;
    }
    return true;
}

inline bool operator<=(const Func& from, const Func& to) {
    if (from.immutable && !to.immutable) {
        return false;
    }
    return (to.params <= from.params) &&  // contravariance
           (from.ret <= to.ret);          // covariance
}

inline bool operator<=(const Array& from, const Array& to) {
    if (from.immutable && !to.immutable) return false;
    if (!(from.elem <= to.elem)) return false;
    if (!to.elem.immutable() && !(to.elem <= from.elem)) return false;
    return from.size >= to.size;
}

inline bool operator<=(const Array& from, const Pointer& to) {
    if (from.immutable && !to.immutable) return false;
    if (!(from.elem <= to.elem)) return false;
    if (!to.elem.immutable() && !(to.elem <= from.elem)) return false;
    return true;
}

inline bool operator<=(const Pointer& from, const Pointer& to) {
    if (from.immutable && !to.immutable) return false;
    if (!(from.elem <= to.elem)) return false;
    if (!to.elem.immutable() && !(to.elem <= from.elem)) return false;
    return true;
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

inline void Sum::append(TypeBox item) {
    if (item.is<Bottom>()) return;  // ⊥ does not add any information
    for (const auto& i : items_) {
        if (i == item) return;
    }
    items_.push_back(std::move(item));
}

inline size_t Sum::index_of(const TypeBox& item) const {
    for (size_t i = 0; i < items_.size(); i++) {
        if (items_[i] == item) {
            return i;
        }
    }
    throw CompilerError(fmt::format("Type {} not found in Sum type {}", item, *this));
}

inline TypeBox operator|(const TypeBox& lhs, const TypeBox& rhs) {
    if (lhs.is<Bottom>()) return rhs;
    if (rhs.is<Bottom>()) return lhs;
    if (lhs == rhs) return lhs;

    auto collect = std::vector<TypeBox>{};
    auto append_to_sum = [&](const TypeBox& b) {
        TypeBox::match(b)(
            [&](const Sum& s) {
                for (const auto& item : s.items_) collect.push_back(item);
            },
            [&](const auto&) { collect.push_back(b); });
    };

    append_to_sum(lhs);
    append_to_sum(rhs);

    auto unique = std::vector<TypeBox>();

    for (auto& item : collect) {
        if (std::all_of(unique.begin(), unique.end(),
                        [&](const TypeBox& u) { return !(item == u); }))
            unique.push_back(std::move(item));
    }

    if (unique.size() == 1) return unique[0];
    return Sum(std::move(unique)).toBoxed();
}

inline bool constructable(const TypeBox& from_box, const TypeBox& to_box) {
    if (from_box.is<Array>() && to_box.is<Array>()) {
        const auto& from = from_box.as<Array>();
        const auto& to = to_box.as<Array>();
        // NOTE: flat array can construct multi-dim array
        if (!from.elem.is<Array>() && to.elem.is<Array>()) {
            auto flattened_to = to_box.flatten();
            if (!flattened_to.is<Array>()) return false;
            const auto& fa = flattened_to.as<Array>();
            return constructable(from.elem, fa.elem) && fa.size >= from.size;
        }
        if (!to.elem.is<Array>() && from.elem.is<Array>()) {
            // NOTE: multi-dim array can not construct flat array
            return false;
        }
        return constructable(from.elem, to.elem) && from.size <= to.size;
    }
    if (from_box.is<Pointer>() && to_box.is<Pointer>()) {
        const auto& from = from_box.as<Pointer>();
        const auto& to = to_box.as<Pointer>();
        return constructable(from.elem, to.elem);
    }
    // Array -> Pointer is not constructable (even though Array <= Pointer by subtyping)
    if (from_box.is<Array>() && to_box.is<Pointer>()) return false;
    if (from_box.is<Pointer>() && to_box.is<Array>()) return false;
    return from_box <= to_box;
}

}  // namespace adt
