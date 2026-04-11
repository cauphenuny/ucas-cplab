/// @brief algebraic data types for IR

#pragma once

#include "utils/error.hpp"
#define FMT_HEADER_ONLY
#include "fmt/format.h"
#include "utils/serialize.hpp"
#include "utils/traits.hpp"

#include <algorithm>
#include <any>
#include <cstddef>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace ir::type {

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
    template <typename T> [[nodiscard]] auto is() const -> bool;
    template <typename T> [[nodiscard]] auto as() const -> const T&;

    static auto match(const Type& type) -> Match<const Type&>;
    static auto match(const TypeBox& box) -> Match<const Type&>;
    template <typename T, typename... Rest>
    static auto match(T&& box, Rest&&... rest) -> decltype(auto);

    [[nodiscard]] auto decay(bool readonly = false) const -> TypeBox;
    [[nodiscard]] auto flatten() const -> TypeBox;
};

struct Int : mixin::ToBoxed<Int, Type> {
    using type = int;
    SIMPLE_TO_STRING("i32");
};

struct Float : mixin::ToBoxed<Float, Type> {
    using type = float;
    SIMPLE_TO_STRING("f32");
};

struct Double : mixin::ToBoxed<Double, Type> {
    using type = double;
    SIMPLE_TO_STRING("f64");
};

struct Bool : mixin::ToBoxed<Bool, Type> {
    using type = bool;
    SIMPLE_TO_STRING("bool");
};

template <typename T> struct is_primitive : std::false_type {};
template <> struct is_primitive<Int> : std::true_type {};
template <> struct is_primitive<Float> : std::true_type {};
template <> struct is_primitive<Double> : std::true_type {};
template <> struct is_primitive<Bool> : std::true_type {};

template <typename T> inline constexpr bool is_primitive_v = is_primitive<T>::value;

struct Bottom : mixin::ToBoxed<Bottom, Type> {
    SIMPLE_TO_STRING("⊥");
};

struct Top : mixin::ToBoxed<Top, Type> {
    SIMPLE_TO_STRING("⊤");
};

struct Product : mixin::ToBoxed<Product, Type> {
    [[nodiscard]] std::string toString() const {
        switch (items_.size()) {
            case 0: return "()";
            case 1: return fmt::format("({},)", items_[0]);
            default:
                std::string result;
                for (size_t i = 0; i < items_.size(); i++) {
                    result += fmt::format("{}{}", items_[i], i == items_.size() - 1 ? "" : ", ");
                }
                return fmt::format("({})", result);
        }
    }
    void append(TypeBox item);
    [[nodiscard]] const auto& items() const {
        return items_;
    }

private:
    std::vector<TypeBox> items_;
};

struct Sum : mixin::ToBoxed<Sum, Type> {
    [[nodiscard]] std::string toString() const {
        std::string result;
        for (size_t i = 0; i < items_.size(); i++) {
            result += fmt::format("{}{}", items_[i], i == items_.size() - 1 ? "" : " | ");
        }
        return fmt::format("({})", result);
    }
    void append(TypeBox item);
    [[nodiscard]] const auto& items() const {
        return items_;
    }

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
    TypeBox params;
    TypeBox ret;
    Func(TypeBox params, TypeBox ret) : params(std::move(params)), ret(std::move(ret)) {}
    SIMPLE_TO_STRING(fmt::format("{} -> {}", params, ret))
};

/// Unsized array / pointer type: &[elem]
struct Pointer : mixin::ToBoxed<Pointer, Type> {
    TypeBox elem;
    bool readonly;
    Pointer(TypeBox elem, bool readonly = false) : elem(std::move(elem)), readonly(readonly) {}
    SIMPLE_TO_STRING(fmt::format("&{}[{}]", readonly ? "" : "mut", elem));
};

/// Sized array type: [elem; size]
struct Array : mixin::ToBoxed<Array, Type> {
    TypeBox elem;
    size_t size;
    Array(TypeBox elem, size_t size) : elem(std::move(elem)), size(size) {}
    SIMPLE_TO_STRING(fmt::format("[{}; {}]", elem, size));
    [[nodiscard]] auto flatten() const -> Array;
    [[nodiscard]] auto decay(bool readonly = false) const -> Pointer;
};

inline std::string TypeBox::toString() const {
    return serialize(*item);
}

inline const Type& TypeBox::var() const {
    return *item;
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
    return Pointer(elem, readonly);
}

inline auto TypeBox::decay(bool readonly) const -> TypeBox {
    return Match{*item}([&](const Array& arr) -> TypeBox { return arr.decay(readonly).toBoxed(); },
                        [&](const auto&) { return *this; });
}

inline auto TypeBox::flatten() const -> TypeBox {
    return Match{*item}([&](const Array& arr) -> TypeBox { return arr.flatten().toBoxed(); },
                        [&](const auto&) { return *this; });
}

inline size_t size_of(const Type& type);
inline size_t size_of(const TypeBox& type_box);

template <typename T, typename = std::enable_if_t<is_primitive_v<T>>>
inline size_t size_of(const T&) {
    return sizeof(typename T::type);
}

inline size_t size_of(const Primitive& prim) {
    return Match{prim}([](const auto& t) { return size_of(t); });
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

template <typename T> struct construct_func;

template <typename R, typename... Args> struct construct_func<R(Args...)> {
    static TypeBox apply() {
        auto params = Product{};
        (params.append(construct<Args>()), ...);
        auto ret = construct<R>();
        return Func(std::move(params).toBoxed(), std::move(ret)).toBoxed();
    }
};

template <typename T> struct construct_product;

template <typename... Args> struct construct_product<std::tuple<Args...>> {
    static TypeBox apply() {
        auto params = Product{};
        (params.append(construct<Args>()), ...);
        return std::move(params).toBoxed();
    }
};

template <typename T> struct construct_sum;

template <typename... Args> struct construct_sum<std::variant<Args...>> {
    static TypeBox apply() {
        auto items = std::vector<TypeBox>{construct<Args>()...};
        return Sum(std::move(items)).toBoxed();
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
        return Pointer(construct<std::remove_pointer_t<Raw>>()).toBoxed();
    } else if constexpr (std::is_array_v<U>) {
        return Array(construct<std::remove_extent_t<Raw>>(), std::extent_v<U>).toBoxed();
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

template <typename T, typename = std::enable_if_t<is_primitive_v<T>>>
bool operator<=(const T&, const T&) {
    return true;  // same type is always a subtype
}

template <typename T, typename = std::enable_if_t<is_primitive_v<T>>>
bool operator<=(const Primitive& from, const T& to) {
    return Match{from}([&](const auto& from) -> bool { return from <= to; });
}

template <typename T, typename = std::enable_if_t<is_primitive_v<T>>>
bool operator<=(const T& from, const Primitive& to) {
    return Match{to}([&](const auto& to) -> bool { return from <= to; });
}

inline bool operator<=(const Primitive& from, const Primitive& to) {
    return Match{from, to}([](const auto& from, const auto& to) -> bool { return from <= to; });
}

inline bool operator<=(const Product& from, const Product& to) {
    if (from.items().size() != to.items().size()) {
        return false;
    }
    for (size_t i = 0; i < from.items().size(); i++) {
        if (!(from.items()[i] <= to.items()[i])) {
            return false;
        }
    }
    return true;
}

template <typename T>
std::enable_if_t<!std::disjunction_v<std::is_same<T, TypeBox>, std::is_same<T, Type>>, bool>
operator<=(const Sum& from, const T& to) {
    return std::all_of(from.items().begin(), from.items().end(),
                       [&](const TypeBox& item) { return item <= to; });
}

inline bool operator<=(const Sum& from, const Sum& to) {  // forall T in from s.t. T -> to
    return std::all_of(from.items().begin(), from.items().end(),
                       [&](const TypeBox& item) { return item <= to; });
}

template <typename T>
std::enable_if_t<!std::disjunction_v<std::is_same<T, TypeBox>, std::is_same<T, Type>>, bool>
operator<=(const T& from, const Sum& to) {
    return std::any_of(to.items().begin(), to.items().end(),
                       [&](const TypeBox& item) { return from <= item; });
}

inline bool operator<=(const Func& from, const Func& to) {
    return (to.params <= from.params) &&  // contravariance
           (from.ret <= to.ret);          // covariance
}

inline bool operator<=(const Array& from, const Array& to) {
    if (!(from.elem <= to.elem)) return false;
    if (!(to.elem <= from.elem)) return false;
    return from.size == to.size;
}

inline bool operator<=(const Pointer& from, const Pointer& to) {
    auto from_elem = from.elem.decay(from.readonly);
    auto to_elem = to.elem.decay(to.readonly);
    if (!(from_elem <= to_elem)) return false;
    if (from.readonly && !to.readonly) return false;
    if (!to.readonly && !(to_elem <= from_elem)) return false;
    return true;
}

inline bool operator<=(const Array& from, const Pointer& to) {
    return from.decay(false) <= to;
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
                for (const auto& item : s.items()) collect.push_back(item);
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

inline size_t dim(const Array& arr) {
    if (arr.elem.is<Array>()) {
        return 1 + dim(arr.elem.as<Array>());
    }
    return 1;
}

inline bool constructable(const TypeBox& from_box, const TypeBox& to_box) {
    if (from_box.is<Array>() && to_box.is<Array>()) {
        const auto& from = from_box.as<Array>();
        const auto& to = to_box.as<Array>();
        // NOTE: flat array can construct multi-dim array
        auto dim_from = dim(from);
        auto dim_to = dim(to);
        if (dim_from > 1 && dim_to != dim_from)
            return false;  // from is not flatten, then dims must match
        if (dim_from == 1 && dim_to > 1) {
            auto flattened_to = to_box.flatten();
            if (!flattened_to.is<Array>()) return false;
            const auto& fa = flattened_to.as<Array>();
            return constructable(from.elem, fa.elem) && fa.size >= from.size;
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

}  // namespace ir::type
