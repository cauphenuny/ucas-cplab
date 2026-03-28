#include "backend/ir/adt.h"

#include <cassert>

void test_subtype() {
    using namespace adt;

    auto i = construct<int>();
    auto f = construct<float>();
    auto b = construct<bool>();
    auto v = construct<void>();

    // Primitives
    assert(isSubtype(i, i));
    assert(isSubtype(f, f));
    assert(isSubtype(b, b));
    assert(!isSubtype(i, f));
    assert(!isSubtype(i, b));

    // Products
    auto t1 = construct<std::tuple<int, float>>();
    auto t2 = construct<std::tuple<int, float>>();
    auto t3 = construct<std::tuple<float, int>>();
    auto t4 = construct<std::tuple<int, float, bool>>();

    assert(isSubtype(t1, t2));
    assert(!isSubtype(t1, t3));
    assert(!isSubtype(t1, t4));

    // Sums (Source)
    auto s1 = construct<std::variant<int, float>>();
    assert(isSubtype(s1, s1));
    assert(!isSubtype(s1, i)); // {int, float} is not always int

    // Sums (Target)
    assert(isSubtype(i, s1)); // int can be converted to {int, float}
    assert(isSubtype(f, s1)); // float can be converted to {int, float}
    assert(!isSubtype(b, s1)); // bool cannot be converted to {int, float}

    // Sums (Both)
    auto s2 = construct<std::variant<int, float, bool>>();
    assert(isSubtype(s1, s2)); // {int, float} -> {int, float, bool} is ok
    assert(!isSubtype(s2, s1)); // {int, float, bool} -> {int, float} is not ok (bool missing)

    // Pointers
    auto pi = construct<int*>();
    auto pf = construct<float*>();
    auto ppi = construct<int**>();
    assert(isSubtype(pi, pi));
    assert(!isSubtype(pi, pf));
    assert(!isSubtype(pi, ppi));

    // Arrays
    auto a10 = construct<int[10]>();
    auto a5 = construct<int[5]>();
    auto af10 = construct<float[10]>();
    assert(isSubtype(a10, a10));
    assert(isSubtype(a10, a5)); // a[10] can be used as a[5] (if we follow C/C++ or similar rules)
    assert(!isSubtype(a5, a10));
    assert(!isSubtype(a10, af10));

    // Functions
    auto f1 = construct<int(int)>();
    auto f2 = construct<int(int)>();
    auto f3 = construct<float(int)>();
    auto f4 = construct<int(float)>();
    assert(isSubtype(f1, f2));
    assert(!isSubtype(f1, f3));
    assert(!isSubtype(f1, f4));

    // Contra-variant parameters
    // Sum s1 = {int, float}, s2 = {int, float, bool}
    // s1 -> s2 (s1 is "smaller" than s2)
    // Func: f1 = (s2 -> int), f2 = (s1 -> int)  -- parameter s1 is convertible to s2?
    // if I have a function that takes {int, float, bool}, I can pass it a function that expects {int, float}?
    // The target will call it with s1. s1 is convertible to s2. So it's OK.
    auto fs2 = construct<int(std::variant<int, float, bool>)>();
    auto fs1 = construct<int(std::variant<int, float>)>();
    assert(isSubtype(fs2, fs1)); // OK: (s2->int) is convertible to (s1->int)
    assert(!isSubtype(fs1, fs2)); // ERR: (s1->int) is not convertible to (s2->int)

    // Co-variant return
    auto frs1 = construct<std::variant<int, float>(int)>();
    auto frs2 = construct<std::variant<int, float, bool>(int)>();
    assert(isSubtype(frs1, frs2)); // OK: (int->s1) is convertible to (int->s2)
    assert(!isSubtype(frs2, frs1)); // ERR

    fmt::println("All convertible tests passed!");
}

void test_construct() {
    using namespace adt;
    fmt::println("int: {}", construct<int>());
    fmt::println("float: {}", construct<float>());
    fmt::println("bool: {}", construct<bool>());
    fmt::println("void: {}", construct<void>());
    fmt::println("int*: {}", construct<int*>());
    fmt::println("int[10]: {}", construct<int[10]>());
    fmt::println("int(int): {}", construct<int(int)>());
    fmt::println("tuple<int, float>: {}", construct<std::tuple<int, float>>());
    fmt::println("variant<int, float>: {}", construct<std::variant<int, float>>());
    fmt::println(
        "Complex: {}", construct<int (*(*(*)(float, std::variant<int, float> (*)(bool))))(float, int*)>());
}

int main() {
    fmt::println("Testing construct...");
    test_construct();

    fmt::println("\nTesting subtype...");
    test_subtype();

    return 0;
}