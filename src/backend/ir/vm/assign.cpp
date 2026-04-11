#include "backend/ir/ir.hpp"
#include "backend/ir/type.hpp"
#include "backend/ir/vm/view.hpp"
#include "utils/error.hpp"
#include "utils/match.hpp"
#include "vm.h"

#include <cstddef>
#include <cstring>
#include <type_traits>
#include <vector>

namespace ir::vm {

void VirtualMachine::assign(const ir::type::Primitive& dest_type, std::byte* dest,
                            const ir::type::Primitive& src_type, const std::byte* src) const {
    Match(dest_type, src_type)([&](auto dest_prim, auto src_prim) {
        if constexpr (std::is_same_v<decltype(dest_prim), decltype(src_prim)>) {
            memcpy(dest, src, ir::type::size_of(dest_type));
        } else {
            throw COMPILER_ERROR(fmt::format("Cannot assign {} to {}", src_type, dest_type));
        }
    });
}

void VirtualMachine::assign(const ir::type::Sum& dest_type, std::byte* dest,
                            const ir::Type& src_type, const std::byte* src) const {
    if (src_type.is<ir::type::Sum>()) {
        auto [src_tag, src_data] = as_sumtype(src);
        auto& src_item_type = src_type.as<ir::type::Sum>().type_of(src_tag);
        auto dest_index = dest_type.index_of(src_item_type);
        auto [dest_tag, dest_data] = as_sumtype(dest);
        dest_tag = dest_index;
        assign(src_item_type, dest_data, src_item_type, src_data);
    } else {
        auto index = dest_type.index_of(src_type);
        auto [tag, data] = as_sumtype(dest);
        tag = index;
        assign(src_type, data, src_type, src);
    }
}

void VirtualMachine::assign(const ir::type::Array& dest_type, std::byte* dest,
                            const ir::type::Array& src_type, const std::byte* src) const {
    auto dest_type_flatten = dest_type.flatten();
    auto src_type_flatten = src_type.flatten();
    if (dest_type_flatten.size < src_type_flatten.size) {
        throw COMPILER_ERROR(fmt::format("Cannot assign array of size {} to array of size {}",
                                         src_type_flatten.size, dest_type_flatten.size));
    }
    size_t i = 0;
    auto src_view = as_array(src, ir::type::size_of(src_type_flatten.elem));
    auto dest_view = as_array(dest, ir::type::size_of(dest_type_flatten.elem));
    for (i = 0; i < src_type_flatten.size; i++) {
        assign(dest_type_flatten.elem, dest_view[i], src_type_flatten.elem, src_view[i]);
    }
}

void VirtualMachine::assign(const ir::type::Pointer& dest_type, std::byte* dest,
                            const ir::type::Pointer& src_type, const std::byte* src) const {
    if (!(src_type.elem <= dest_type.elem)) {
        throw COMPILER_ERROR(fmt::format("Cannot assign pointer of type {} to pointer of type {}",
                                         src_type, dest_type));
    }
    memcpy(dest, src, ir::type::size_of(dest_type));
}

void VirtualMachine::assign(const ir::type::Pointer& dest_type, std::byte* dest,
                            const ir::type::Array& src_type, const std::byte* src) const {
    if (!(src_type.elem <= dest_type.elem)) {
        throw COMPILER_ERROR(fmt::format("Cannot assign array of type {} to pointer of type {}",
                                         src_type, dest_type));
    }
    memcpy(dest, (void*)&src, ir::type::size_of(dest_type));
}

void VirtualMachine::assign(const ir::type::Product& dest_type, std::byte* dest,
                            const ir::type::Product& src_type, const std::byte* src) const {
    if (dest_type.items().size() != src_type.items().size()) {
        throw COMPILER_ERROR(fmt::format("Cannot assign {} to {}", src_type, dest_type));
    }
    size_t dest_offset = 0, src_offset = 0;
    for (size_t i = 0; i < dest_type.items().size(); i++) {
        auto& dest_item = dest_type.items()[i];
        auto& src_item = src_type.items()[i];
        assign(dest_item, (std::byte*)dest + dest_offset, src_item,
               (const std::byte*)src + src_offset);
        dest_offset += ir::type::size_of(dest_item);
        src_offset += ir::type::size_of(src_item);
    }
}

void VirtualMachine::assign(const Type& dest_type, std::byte* dest, const Type& src_type,
                            const std::byte* src) const {
    Match(dest_type.var(), src_type.var())(
        [&](const auto& dest_var, const auto& src_var) { assign(dest_var, dest, src_var, src); });
}

void VirtualMachine::assign(View& dest, const View& src) const {
    if (dest.type.is<ir::type::Sum>()) {
        auto& sum_type = dest.type.as<ir::type::Sum>();
        assign(sum_type, dest.data, src.type, src.data);
    } else {
        assign(dest.type, dest.data, src.type, src.data);
    }
}

}  // namespace ir::vm