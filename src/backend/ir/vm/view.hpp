#include "../ir.hpp"

#include <cstddef>

namespace ir::vm {

struct View {
    std::byte* data;
    ir::Type type;
};

template <typename T, typename = std::enable_if_t<std::disjunction_v<
                          std::is_same<T, std::byte>, std::is_same<T, const std::byte>>>>
struct ArrayView {
    size_t size;
    T* data;
    T* operator[](size_t index) {
        return (T*)((std::byte*)data + index * size);
    }
};

template <typename T, typename = std::enable_if_t<std::disjunction_v<
                          std::is_same<T, std::byte>, std::is_same<T, const std::byte>>>>
struct SumView {
    using Tag = std::conditional_t<std::is_const_v<T>, const int, int>;
    Tag& tag;
    T* data;
};

inline auto as_array(std::byte* data, size_t elem_size) {
    return ArrayView<std::byte>{elem_size, data};
}

inline auto as_array(const std::byte* data, size_t elem_size) {
    return ArrayView<const std::byte>{elem_size, data};
}

inline auto as_sumtype(std::byte* data) {
    return SumView<std::byte>{*(int*)data, data + sizeof(int)};
}

inline auto as_sumtype(const std::byte* data) {
    return SumView<const std::byte>{*(const int*)data, data + sizeof(int)};
}

}  // namespace ir::vm