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

template <typename... Ts> struct Match {
    std::tuple<Ts...> values;
    Match(Ts&&... val) : values(std::forward<Ts>(val)...) {}
    template <typename... Fs> auto operator()(Fs&&... callbacks) {
        return std::apply(
            [&](auto&&... vs) {
                return std::visit(Visitor{std::forward<Fs>(callbacks)...},
                                  std::forward<decltype(vs)>(vs)...);
            },
            values);
    }
    template <typename T> Match<Ts..., T> with(T&& val) && {
        return Match<Ts..., T>(std::forward<Ts>(values)..., std::forward<T>(val));
    }
    template <typename... T2s> Match<Ts..., T2s...> with(Match<T2s...>&& other) && {
        return std::apply(
            [](auto&&... args) {
                return Match<Ts..., T2s...>(std::forward<decltype(args)>(args)...);
            },
            std::tuple_cat(std::move(values), std::move(other.values))
        );
    }
};

template <typename... Ts> Match(Ts&&...) -> Match<Ts&&...>;

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
