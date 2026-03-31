#include "backend/ir/type.hpp"

#include <cassert>

void test_subtype() {
    using namespace adt;

    auto i = construct<int>();
    auto f = construct<float>();
    auto b = construct<bool>();
    auto v = construct<void>();

    // Primitives
    assert(i <= i);
    assert(f <= f);
    assert(b <= b);
    assert(!(i <= f));
    assert(!(i <= b));

    // Products
    auto t1 = construct<std::tuple<int, float>>();
    auto t2 = construct<std::tuple<int, float>>();
    auto t3 = construct<std::tuple<float, int>>();
    auto t4 = construct<std::tuple<int, float, bool>>();

    assert(t1 <= t2);
    assert(!(t1 <= t3));
    assert(!(t1 <= t4));

    // Sums (Source)
    auto s1 = construct<std::variant<int, float>>();
    assert(s1 <= s1);
    assert(!(s1 <= i)); // {int, float} is not always int

    // Sums (Target)
    assert(i <= s1); // int can be converted to {int, float}
    assert(f <= s1); // float can be converted to {int, float}
    assert(!(b <= s1)); // bool cannot be converted to {int, float}

    // Sums (Both)
    auto s2 = construct<std::variant<int, float, bool>>();
    assert(s1 <= s2); // {int, float} -> {int, float, bool} is ok
    assert(!(s2 <= s1)); // {int, float, bool} -> {int, float} is not ok (bool missing)

    // Pointers
    auto pi = construct<int*>();
    auto pf = construct<float*>();
    auto ppi = construct<int**>();
    assert(pi <= pi);
    assert(!(pi <= pf));
    assert(!(pi <= ppi));

    // Arrays
    auto a10 = construct<int[10]>();
    auto a5 = construct<int[5]>();
    auto af10 = construct<float[10]>();
    assert(a10 <= a10);
    assert(a10 <= a5); // a[10] can be used as a[5] (if we follow C/C++ or similar rules)
    assert(!(a5 <= a10));
    assert(!(a10 <= af10));
    assert(a10 <= pi);
    assert(!(pi <= a10));

    // Functions
    auto f1 = construct<int(int)>();
    auto f2 = construct<int(int)>();
    auto f3 = construct<float(int)>();
    auto f4 = construct<int(float)>();
    assert(f1 <= f2);
    assert(!(f1 <= f3));
    assert(!(f1 <= f4));

    // Contra-variant parameters
    // Sum s1 = {int, float}, s2 = {int, float, bool}
    // s1 -> s2 (s1 is "smaller" than s2)
    // Func: f1 = (s2 -> int), f2 = (s1 -> int)  -- parameter s1 is convertible to s2?
    // if I have a function that takes {int, float, bool}, I can pass it a function that expects {int, float}?
    // The target will call it with s1. s1 is convertible to s2. So it's OK.
    auto fs2 = construct<int(std::variant<int, float, bool>)>();
    auto fs1 = construct<int(std::variant<int, float>)>();
    assert(fs2 <= fs1); // OK: (s2->int) is convertible to (s1->int)
    assert(!(fs1 <= fs2)); // ERR: (s1->int) is not convertible to (s2->int)

    // Co-variant return
    auto frs1 = construct<std::variant<int, float>(int)>();
    auto frs2 = construct<std::variant<int, float, bool>(int)>();
    assert(frs1 <= frs2); // OK: (int->s1) is convertible to (int->s2)
    assert(!(frs2 <= frs1)); // ERR

    // Top and bottom
    auto top = construct<std::any>();
    auto bottom = Bottom{};
    assert(top <= top);
    assert(bottom <= bottom);
    assert(bottom <= top);
    assert(!(top <= bottom));
    assert(frs2 <= top);
    assert(bottom <= frs1);
    auto sometimes_top = construct<std::variant<int, std::any>>();
    assert(frs1 <= sometimes_top);

    // Equality and union/operator| tests
    assert(i == i);
    assert(!(i == f));
    auto s_union = (i | f);
    assert(s_union == s1);
    auto s = adt::Sum{};
    s.append(construct<int>());
    assert(s == construct<int>());
    auto never = TypeBox{Bottom{}.toBoxed()};
    assert((never | construct<int>()) == construct<int>());
    assert((never | never) == never);
    assert((never | construct<void>()) == construct<void>());

    // constructable
    assert(constructable(construct<int[5]>(), construct<int[10]>())); // smaller array can construct bigger array
    assert(!constructable(construct<int[10]>(), construct<int[5]>()));
    assert(!constructable(construct<int[5]>(), construct<int*>())); // array cannot construct pointer
    assert(
        !constructable(construct<int*>(), construct<int[5]>()));  // pointer cannot construct array
    assert(constructable(construct<int[6]>(), construct<int[2][3]>())); // flat array can construct multi-dim array

    fmt::println("All subtype tests passed!");
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
    fmt::println("any: {}", construct<std::any>());
    fmt::println(
        "Complex: {}", construct<int (*(*(*)(float, std::variant<int, float> (*)(bool))))(float, int*)>());
    
    fmt::println("\nUnion tests:");
    auto never = TypeBox{Bottom{}.toBoxed()};
    auto i = construct<int>();
    auto v = construct<void>();
    fmt::println("NEVER | INT: {}", never | i);
    fmt::println("NEVER | NEVER: {}", never | never);
    fmt::println("VOID | NEVER: {}", v | never);
    fmt::println("INT | INT: {}", i | i);
}

void test_slice() {
    using namespace adt;
    auto slice1 = construct<int[10]>();
    fmt::println("decay({}): {}", slice1, slice1.decay());
    auto slice2 = construct<int[10][20]>();
    fmt::println("flatten({}): {}", slice2, slice2.flatten());
    auto slice3 = construct<int[10][20]>();
    fmt::println("decay({}): {}", slice3, slice3.decay());
    auto slice4 = construct<int[10][20][30]>();
    fmt::println("flatten({}): {}", slice4, slice4.flatten());
}

int main() {
    fmt::println("Testing construct...");
    test_construct();

    fmt::println("\nTesting subtype...");
    test_subtype();

    fmt::println("\nTesting slice decay and flatten...");
    test_slice();
    return 0;
}