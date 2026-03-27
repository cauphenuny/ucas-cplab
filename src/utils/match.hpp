#pragma once

#include <utility>
#include <variant>

template <typename... Ts> struct Visitor : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> Visitor(Ts...) -> Visitor<Ts...>;

template <typename T, typename... Fs> auto match(T&& expr, Fs&&... callbacks) {
    return std::visit(Visitor{std::forward<Fs>(callbacks)...}, std::forward<T>(expr));
}

template <typename T> struct Match {
    T value;
    Match(T&& val) : value(std::forward<T>(val)) {}
    template <typename... Fs> auto operator()(Fs&&... callbacks) {
        return std::visit(Visitor{std::forward<Fs>(callbacks)...}, std::forward<T>(value));
    }
    template <typename... Ts> auto operator()(Visitor<Ts...> visitor) {
        return std::visit(std::move(visitor), std::forward<T>(value));
    }
};

template <typename T> Match(T&&) -> Match<T&&>;

/*
using T = std::variant<int, double>;
T v = 1.414;

auto result = Match{v}(
    [](int x) -> T {
        std::cout << "get int value: " << x << std::endl;
        return x * x;
    },
    [](double x) -> T {
        std::cout << "get double value: " << x << std::endl;
        return x * x;
    }
);

// or:

auto result2 = match(
    v,
    [](int x) -> T {
        std::cout << "get int value: " << x << std::endl;
        return x * x;
    },
    [](double x) -> T {
        std::cout << "get double value: " << x << std::endl;
        return x * x;
    }
);
*/
