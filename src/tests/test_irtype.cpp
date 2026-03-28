#include "backend/ir/type.h"

int main() {
    using namespace ir_type;
    fmt::println("{}", construct<int>());
    fmt::println("{}", construct<float>());
    fmt::println("{}", construct<bool>());
    fmt::println("{}", construct<void>());
    fmt::println("{}", construct<int*>());
    fmt::println("{}", construct<int[10]>());
    fmt::println("{}", construct<int(int)>());
    fmt::println("{}", construct<std::tuple<int, float>>());
    fmt::println("{}", construct<std::variant<int, float>>());
    fmt::println(
        "{}", construct<int (*(*(*)(float, std::variant<int, float> (*)(bool))))(float, int*)>());
    return 0;
}