#include "vm.h"
#include <cstddef>

namespace ir::vm {

void VirtualMachine::assign(const adt::Primitive& dest_type, std::byte* dest,
                            const adt::Primitive& src_type, const std::byte* src) const {
    Match(dest_type, src_type)([&](auto dest_prim, auto src_prim) {
        if constexpr (std::is_same_v<decltype(dest_prim), decltype(src_prim)>) {
            memcpy(dest, src, adt::size_of(dest_type));
        } else {
            throw CompilerError(fmt::format("Cannot assign {} to {}", src_type, dest_type));
        }
    });
}

void VirtualMachine::assign(const adt::Sum& dest_type, std::byte* dest, const ir::Type& src_type,
                            const std::byte* src) const {
    if (src_type.is<adt::Sum>()) {
        auto [src_tag, src_data] = as_sumtype(src);
        auto& src_item_type = src_type.as<adt::Sum>().type_of(src_tag);
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

void VirtualMachine::assign(const adt::Array& dest_type, std::byte* dest, const adt::Array& src_type,
                            const std::byte* src) const {
    if (dest_type.size < src_type.size) {
        throw CompilerError(fmt::format("Cannot assign array of size {} to array of size {}",
                                        src_type.size, dest_type.size));
    }
    size_t i = 0;
    auto src_view = as_array(src, adt::size_of(src_type.elem));
    auto dest_view = as_array(dest, adt::size_of(dest_type.elem));
    for (i = 0; i < src_type.size; i++) {
        assign(dest_type.elem, dest_view[i], src_type.elem, src_view[i]);
    }
    for (; i < dest_type.size; i++) {
        memset(dest_view[i], 0, adt::size_of(dest_type.elem));
    }
}

void VirtualMachine::assign(const adt::Product& dest_type, std::byte* dest, const adt::Product& src_type,
                            const std::byte* src) const {
    if (dest_type.items().size() != src_type.items().size()) {
        throw CompilerError(fmt::format("Cannot assign {} to {}", src_type, dest_type));
    }
    size_t dest_offset = 0, src_offset = 0;
    for (size_t i = 0; i < dest_type.items().size(); i++) {
        auto& dest_item = dest_type.items()[i];
        auto& src_item = src_type.items()[i];
        assign(dest_item, (std::byte*)dest + dest_offset, src_item, (const std::byte*)src + src_offset);
        dest_offset += adt::size_of(dest_item);
        src_offset += adt::size_of(src_item);
    }
}

void VirtualMachine::assign(View dest, const View& src) const {
    if (dest.type.is<adt::Sum>()) {
        auto& sum_type = dest.type.as<adt::Sum>();
        assign(sum_type, dest.data, src.type, src.data);
    } else {
        Match{dest.type.var(), src.type.var()}([&](const auto& dest_type, const auto& src_type) {
            assign(dest_type, dest.data, src_type, src.data);
        });
    }
}

}  // namespace ir::vm