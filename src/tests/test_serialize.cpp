#include "utils/serialize.hpp"

#include <variant>

struct Inner {
    int a = 1;
    int b = 2;
    TO_STRING(Inner, a, b);
};

struct Outer {
    std::string name = "test";
    Inner inner;
    TO_STRING(Outer, name, inner);
};

int main() {
    fmt::println("{}", Outer{});
    std::variant<int, float> x = 3.0f;
    fmt::println("{}", serialize(x));
    return 0;
}