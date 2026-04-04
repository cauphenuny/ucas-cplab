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
    [[nodiscard]] auto comptime() const -> bool;
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
    bool comptime{false};
    SIMPLE_TO_STRING(comptime ? "i32 const" : "i32");
};

struct Float : mixin::ToBoxed<Float, Type> {
    using type = float;
    bool comptime{false};
    SIMPLE_TO_STRING(comptime ? "f32 const" : "f32");
};

struct Double : mixin::ToBoxed<Double, Type> {
    using type = double;
    bool comptime{false};
    SIMPLE_TO_STRING(comptime ? "f64 const" : "f64");
};

struct Bool : mixin::ToBoxed<Bool, Type> {
    using type = bool;
    bool comptime{false};
    SIMPLE_TO_STRING(comptime ? "bool const" : "bool");
};

struct Bottom : mixin::ToBoxed<Bottom, Type> {
    inline constexpr static bool comptime{true};
    SIMPLE_TO_STRING("⊥");
};

struct Top : mixin::ToBoxed<Top, Type> {
    inline constexpr static bool comptime{true};
    SIMPLE_TO_STRING("⊤");
};

struct Product : mixin::ToBoxed<Product, Type> {
    bool comptime{false};
    [[nodiscard]] std::string toString() const {
        switch (items_.size()) {
            case 0: return "()";
            case 1: return fmt::format("({},)", items_[0]);
            default:
                std::string result;
                for (size_t i = 0; i < items_.size(); i++) {
                    result += fmt::format("{}{}", items_[i], i == items_.size() - 1 ? "" : ", ");
                }
                return fmt::format("({}){}", result, comptime ? " const" : "");
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
    bool comptime{false};
    [[nodiscard]] std::string toString() const {
        std::string result;
        for (size_t i = 0; i < items_.size(); i++) {
            result += fmt::format("{}{}", items_[i], i == items_.size() - 1 ? "" : " | ");
        }
        return fmt::format("({}){}", result, comptime ? " const" : "");
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
            throw COMPILER_ERROR("Sum type must have at least 2 items");
        }
    }

    [[nodiscard]] size_t index_of(const TypeBox& item) const;
    [[nodiscard]] const TypeBox& type_of(size_t index) const {
        if (index >= items_.size()) {
            throw COMPILER_ERROR(
                fmt::format("Index {} out of bounds for Sum type {}", index, *this));
        }
        return items_[index];
    }

private:
    std::vector<TypeBox> items_;
};

struct Func : mixin::ToBoxed<Func, Type> {
    bool comptime{false};
    Product params;
    TypeBox ret;
    Func(Product params, TypeBox ret) : params(std::move(params)), ret(std::move(ret)) {}
    SIMPLE_TO_STRING(fmt::format("{}{} -> {}{}", comptime ? "(" : "", params, ret,
                                 comptime ? ") const" : ""))
};

/// Unsized array / pointer type: &[elem]
struct Pointer : mixin::ToBoxed<Pointer, Type> {
    TypeBox elem;
    bool readonly;
    bool comptime{false};
    Pointer(TypeBox elem, bool readonly = false) : elem(std::move(elem)), readonly(readonly) {}
    SIMPLE_TO_STRING(fmt::format("&{}[{}]{}", readonly ? "" : "mut", elem, comptime ? " const" : ""));
};

/// Sized array type: [elem; size]
struct Array : mixin::ToBoxed<Array, Type> {
    bool comptime{false};
    TypeBox elem;
    size_t size;
    Array(TypeBox elem, size_t size) : elem(std::move(elem)), size(size) {}
    SIMPLE_TO_STRING(fmt::format("[{}; {}]{}", elem, size, comptime ? " const" : ""));
    [[nodiscard]] auto flatten() const -> Array;
    [[nodiscard]] auto decay(bool readonly = false) const -> Pointer;
};

inline std::string TypeBox::toString() const {
    return serialize(*item);
}

inline const Type& TypeBox::var() const {
    return *item;
}

inline bool TypeBox::comptime() const {
    return Match{*item}([](const auto& t) -> bool {
        using T = std::decay_t<decltype(t)>;
        if constexpr (std::is_same_v<T, Product> || std::is_same_v<T, Sum> ||
                      std::is_same_v<T, Func> || std::is_same_v<T, Array> ||
                      std::is_same_v<T, Pointer>) {
            return t.comptime;
        } else if constexpr (std::is_same_v<T, Primitive>) {
            return Match{t}([](const auto& prim) -> bool { return prim.comptime; });
        } else {
            return true;  // Top and Bottom are always comptime
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

inline auto Array::decay(bool readonly) const -> Pointer {
    auto p = Pointer(elem, readonly);
    p.comptime = comptime;
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

inline size_t size_of(const Type& type);
inline size_t size_of(const TypeBox& type_box);

inline size_t size_of(const Int&) {
    return sizeof(Int::type);
}

inline size_t size_of(const Float&) {
    return sizeof(Float::type);
}

inline size_t size_of(const Double&) {
    return sizeof(Double::type);
}

inline size_t size_of(const Bool&) {
    return sizeof(Bool::type);
}

inline size_t size_of(const Primitive& prim) {
    return Match{prim}([](const Int& t) { return size_of(t); },
                       [](const Float& t) { return size_of(t); },
                       [](const Double& t) { return size_of(t); },
                       [](const Bool& t) { return size_of(t); });
}

inline size_t size_of(const Product& prod) {
    // TODO: padding and alignment
    size_t size = 0;
    for (const auto& item : prod.items()) {
        size += size_of(item);
    }
    return size;
}

inline size_t size_of(const Sum& sum) {
    size_t max_size = 0;
    for (const auto& item : sum.items()) {
        auto item_size = size_of(item);
        if (item_size > max_size) {
            max_size = item_size;
        }
    }
    return max_size + sizeof(int);  // tag
}

inline size_t size_of(const Func&) {
    // Function values are represented as pointers in VM.
    return sizeof(void*);
}

inline size_t size_of(const Array& arr) {
    return arr.size * size_of(arr.elem);
}

inline size_t size_of(const Pointer&) {
    return sizeof(void*);
}

inline size_t size_of(const Top&) {
    return 0;
}

inline size_t size_of(const Bottom&) {
    return 0;
}

inline size_t size_of(const Type& type) {
    return Match{type}([](const auto& t) { return size_of(t); });
}

inline size_t size_of(const TypeBox& type_box) {
    return size_of(type_box.var());
}

/************************************************************/

template <typename T> TypeBox construct();

namespace {

template <typename T> struct false_v : std::false_type {};

template <typename T> struct is_tuple : std::false_type {};
template <typename... Args> struct is_tuple<std::tuple<Args...>> : std::true_type {};

template <typename T> struct is_variant : std::false_type {};
template <typename... Args> struct is_variant<std::variant<Args...>> : std::true_type {};

template <typename T> T mark_comptime(bool comptime) {
    auto t = T{};
    t.comptime = comptime;
    return t;
}

template <typename T> struct construct_func;

template <typename R, typename... Args> struct construct_func<R(Args...)> {
    static TypeBox apply(bool comptime = false) {
        auto params = Product{};
        (params.append(construct<Args>()), ...);
        auto ret = construct<R>();
        auto func = Func(std::move(params), std::move(ret));
        func.comptime = comptime;
        return std::move(func).toBoxed();
    }
};

template <typename T> struct construct_product;

template <typename... Args> struct construct_product<std::tuple<Args...>> {
    static TypeBox apply(bool comptime = false) {
        auto params = Product{};
        (params.append(construct<Args>()), ...);
        params.comptime = comptime;
        return std::move(params).toBoxed();
    }
};

template <typename T> struct construct_sum;

template <typename... Args> struct construct_sum<std::variant<Args...>> {
    static TypeBox apply(bool comptime = false) {
        auto items = std::vector<TypeBox>{construct<Args>()...};
        auto sum = Sum(std::move(items));
        sum.comptime = comptime;
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
    constexpr bool comptime = std::is_const_v<Raw>;

    if constexpr (std::is_same_v<U, int>) {
        return mark_comptime<Int>(comptime).toBoxed();
    } else if constexpr (std::is_same_v<U, float>) {
        return mark_comptime<Float>(comptime).toBoxed();
    } else if constexpr (std::is_same_v<U, double>) {
        return mark_comptime<Double>(comptime).toBoxed();
    } else if constexpr (std::is_same_v<U, bool>) {
        return mark_comptime<Bool>(comptime).toBoxed();
    } else if constexpr (std::is_same_v<U, std::any>) {
        return Top{}.toBoxed();
    } else if constexpr (std::is_pointer_v<U>) {
        auto t = Pointer(construct<std::remove_pointer_t<Raw>>());
        t.comptime = comptime;
        return std::move(t).toBoxed();
    } else if constexpr (std::is_array_v<U>) {
        auto t = Array(construct<std::remove_extent_t<Raw>>(), std::extent_v<U>);
        t.comptime = comptime;
        return std::move(t).toBoxed();
    } else if constexpr (std::is_void_v<U>) {
        auto t = Product{};
        t.comptime = comptime;
        return std::move(t).toBoxed();
    } else if constexpr (std::is_function_v<U>) {
        return construct_func<U>::apply(comptime);
    } else if constexpr (is_tuple<U>::value) {
        return construct_product<U>::apply(comptime);
    } else if constexpr (is_variant<U>::value) {
        return construct_sum<U>::apply(comptime);
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
    return !to.comptime || from.comptime;  // if to is comptime, from must also be comptime
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
    if (from.comptime && !to.comptime) {
        return false;
    }
    return (to.params <= from.params) &&  // contravariance
           (from.ret <= to.ret);          // covariance
}

inline bool operator<=(const Array& from, const Array& to) {
    if (!(from.elem <= to.elem)) return false;
    if (!(to.elem <= from.elem)) return false;
    return from.size == to.size;
}

inline bool operator<=(const Array& from, const Pointer& to) {
    if (!(from.elem <= to.elem)) return false;
    if (!to.readonly && !(to.elem <= from.elem)) return false;
    return true;
}

inline bool operator<=(const Pointer& from, const Pointer& to) {
    if (!(from.elem <= to.elem)) return false;
    if (!to.readonly && !(to.elem <= from.elem)) return false;
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
    throw COMPILER_ERROR(fmt::format("Type {} not found in Sum type {}", item, *this));
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
