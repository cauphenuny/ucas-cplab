#include "utils/serialize.hpp"

struct Inner {
    int a = 1;
    int b = 2;
    SERIALIZABLE(Inner, a, b);
};

struct Outer {
    std::string name = "test";
    Inner inner;
    SERIALIZABLE(Outer, name, inner);
};

int main() {
    fmt::println("{}", Outer{});
    return 0;
}