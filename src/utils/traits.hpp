#pragma once

#include <memory>
#include <type_traits>

struct Location {
    int line{-1}, col{-1};
    operator bool() const { return line != -1 && col != -1; }
};

namespace mixin {
template <typename Self, typename Target = Self> struct ToBoxed {
private:
    ToBoxed() = default;

public:
    std::unique_ptr<Target> toBoxed() && {
        return std::make_unique<Target>(std::move(*static_cast<Self*>(this)));
    }
    friend Self;
};

struct Locatable {
    Location loc;
};
}  // namespace mixin

namespace traits {

template <typename T, typename = void> struct is_locatable : std::false_type {};

template <typename T>
struct is_locatable<T, std::void_t<decltype(std::declval<T>().loc)>>
    : std::is_same<decltype(std::declval<T>().loc), Location> {};

template <typename T> constexpr bool is_locatable_v = is_locatable<T>::value;

}  // namespace traits