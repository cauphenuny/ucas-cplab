#include "../type.hpp"

namespace ir::vm {

struct DataLayout {

    size_t int_size, float_size, double_size, ptr_size;

    DataLayout(size_t int_size, size_t float_size, size_t double_size, size_t ptr_size)
        : int_size(int_size), float_size(float_size), double_size(double_size), ptr_size(ptr_size) {
    }

    [[nodiscard]] size_t size_of(const adt::Int&) const {
        return int_size;
    }

    [[nodiscard]] size_t size_of(const adt::Float&) const {
        return float_size;
    }

    [[nodiscard]] size_t size_of(const adt::Double&) const {
        return double_size;
    }

    [[nodiscard]] size_t size_of(const adt::Bool&) const {
        return 1;
    }

    [[nodiscard]] size_t size_of(const adt::Primitive& prim) const {
        return Match{prim}([&](const adt::Int&) { return int_size; },
                           [&](const adt::Float&) { return float_size; },
                           [&](const adt::Double&) { return double_size; },
                           [&](const adt::Bool&) -> size_t { return 1; });
    };

    [[nodiscard]] size_t size_of(const adt::Product& prod) const {
        // TODO: padding and alignment
        size_t size = 0;
        for (const auto& item : prod.items()) {
            size += size_of(item);
        }
        return size;
    }

    [[nodiscard]] size_t size_of(const adt::Sum& sum) const {
        size_t max_size = 0;
        for (const auto& item : sum.items()) {
            max_size = std::max(max_size, size_of(item));
        }
        return max_size + int_size;  // tag
    }

    [[nodiscard]] size_t size_of(const adt::Func& func) const {
        // function pointer
        return ptr_size;
    }

    [[nodiscard]] size_t size_of(const adt::Array& arr) const {
        return arr.size * size_of(arr.elem);
    }

    [[nodiscard]] size_t size_of(const adt::Pointer& ptr) const {
        return ptr_size;
    }

    [[nodiscard]] size_t size_of(const adt::Top& top) const {
        return 0;
    }

    [[nodiscard]] size_t size_of(const adt::Bottom& bottom) const {
        return 0;
    }

    [[nodiscard]] size_t size_of(const adt::TypeBox& type_box) const {
        return Match{*type_box.item}([&](const auto& type) { return size_of(type); });
    };
};

}  // namespace ir::vm