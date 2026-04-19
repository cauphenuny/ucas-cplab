#include "backend/ir/ir.h"

#include <cstddef>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ir {

Alloc::Alloc(std::string name, Type type, bool comptime, bool immutable, bool reference,
             std::optional<ConstexprValue> init)
    : name(std::move(name)), type(std::move(type)), comptime(comptime), immutable(immutable),
      reference(reference), init(std::move(init)) {
    if (comptime && !this->init) {
        throw COMPILER_ERROR(
            fmt::format("comptime variable '{}' must have an initializer", this->name));
    }
    if (comptime && !immutable) {
        throw COMPILER_ERROR(fmt::format("comptime variable '{}' must be immutable", this->name));
    }
}

auto Alloc::toString() const -> std::string {
    auto keyword = comptime ? "const" : "let";
    std::string attr;
    attr += reference ? "ref " : "";
    attr += immutable ? "" : "mut ";
    if (init) {
        return fmt::format("{} {}{}: {} = {};", keyword, attr, name, type, *init);
    }
    return fmt::format("{} {}{}: {};", keyword, attr, name, type);
}

auto Alloc::clone() const -> std::unique_ptr<Alloc> {
    return std::make_unique<Alloc>(name, type, comptime, immutable, reference, init);
}

auto Alloc::constant(std::string name, Type type, ConstexprValue init) -> std::unique_ptr<Alloc> {
    return std::make_unique<Alloc>(std::move(name), std::move(type), true, true, false,
                                   std::move(init));
}

auto Alloc::variable(std::string name, Type type, std::optional<ConstexprValue> init,
                     bool immutable) -> std::unique_ptr<Alloc> {
    return std::make_unique<Alloc>(std::move(name), std::move(type), false, immutable, false,
                                   std::move(init));
}

auto Alloc::reference_var(std::string name, Type type, std::optional<ConstexprValue> init,
                          bool immutable) -> std::unique_ptr<Alloc> {
    return std::make_unique<Alloc>(std::move(name), std::move(type), false, immutable, true,
                                   std::move(init));
}

auto Alloc::value() const -> NamedValue {
    if (reference) {
        return {type.borrow(immutable), this};
    }
    return {type, this};
}

}  // namespace ir
