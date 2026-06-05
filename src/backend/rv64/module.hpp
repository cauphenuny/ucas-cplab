#pragma once

#include "backend/ir/ir.h"
#include "backend/ir/type.hpp"
#include "inst.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rv64 {

struct Global {
    std::string name;
    ir::Type type;
    std::optional<ir::ConstexprValue> init;
};

struct FrameLayout {
    size_t total_size{0};
    size_t ra_offset{0};
    std::vector<std::pair<GeneralReg, size_t>> saved_gprs;
    std::vector<std::pair<FloatReg, size_t>> saved_fprs;
    std::unordered_map<const ir::Alloc*, size_t> spill_offsets;

    [[nodiscard]] auto offset_of(const ir::Alloc* alloc) const -> size_t {
        return spill_offsets.at(alloc);
    }
    [[nodiscard]] auto has_spill(const ir::Alloc* alloc) const -> bool {
        return spill_offsets.count(alloc) > 0;
    }
};

struct AsmBlock {
    std::string label;
    std::vector<Inst> insts;
};

struct AsmFunc {
    std::string name;
    FrameLayout frame;
    std::vector<AsmBlock> blocks;
};

struct FloatLiteral {
    std::string label;
    ir::ConstexprValue value;
};

struct Module {
    std::vector<Global> globals;
    std::vector<AsmFunc> funcs;
    std::vector<FloatLiteral> float_literals;
};

}  // namespace rv64
