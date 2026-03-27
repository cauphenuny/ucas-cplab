#pragma once

#include <memory>

namespace traits {
template <typename Self, typename Target = Self> struct ToBoxed {
private:
    ToBoxed() = default;

public:
    std::unique_ptr<Target> toBoxed() && {
        return std::make_unique<Target>(std::move(*static_cast<Self*>(this)));
    }
    friend Self;
};
}  // namespace traits
