#pragma once

#include "backend/rv64/inst.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace rv64::vm {

struct VirtualMachine {
    // Register files
    std::array<uint64_t, 32> regs{};
    std::array<uint64_t, 32> fregs{};

    // Memory
    std::vector<uint8_t> memory;
    static constexpr uint64_t MEM_SIZE = 64 * 1024 * 1024;
    static constexpr uint64_t STACK_INIT = MEM_SIZE - 512;
    static constexpr uint64_t GLOBAL_BASE = 0x1000;

    // Flat instruction list — built in execute()
    struct FlatInst {
        Inst inst;
        bool is_call_builtin{false};
        size_t branch_target{0};
    };
    std::vector<FlatInst> flat_insts;
    std::unordered_map<std::string, size_t> label_map;
    std::unordered_map<std::string, uint64_t> symbol_map;
    std::vector<size_t> call_stack;

    // I/O
    std::istream& input;
    std::ostream& output;

    // Stats
    size_t num_insts{0};
    size_t max_insts{1'000'000'000};

    // Program counter (flat instruction index)
    size_t pc{0};

    // Builtin dispatch table
    using BuiltinFn = void (*)(VirtualMachine&);
    std::unordered_map<std::string, BuiltinFn> builtins;

    // Float helpers for fregs access
    [[nodiscard]] float get_f(size_t id) const {
        float v;
        __builtin_memcpy(&v, &fregs[id], 4);
        return v;
    }
    [[nodiscard]] double get_d(size_t id) const {
        double v;
        __builtin_memcpy(&v, &fregs[id], 8);
        return v;
    }
    void set_f(size_t id, float v) {
        __builtin_memcpy(&fregs[id], &v, 4);
    }
    void set_d(size_t id, double v) {
        __builtin_memcpy(&fregs[id], &v, 8);
    }

    VirtualMachine(std::istream& in, std::ostream& out) : input(in), output(out) {
        init_builtins();
    }

    uint8_t execute(const Module& mod);

private:
    // Initialization helpers
    void build_flat_insts(const Module& mod);
    void place_globals(const Module& mod);
    void resolve_branches();
    void init_builtins();

    // Instruction dispatch
    void exec(const Inst& inst, const FlatInst& fi);
    void exec_r(const InstR& i);
    void exec_fr(const InstFR& i);
    void exec_i(const InstI& i, const FlatInst& fi);
    void exec_fi(const InstFI& i);
    void exec_j(const InstJ& i, const FlatInst& fi);
    void exec_u(const InstU& i);
    void exec_pseudo_r(const PseudoR& i);
    void exec_pseudo_li(const PseudoLI& i);
    void exec_pseudo_b(const PseudoB& i, const FlatInst& fi);
    void exec_pseudo_j(const PseudoJ& i, const FlatInst& fi);
    void exec_pseudo_l(const PseudoL& i);
    void exec_pseudo_ret();

    // Memory accessors
    template <typename T> T load(uint64_t addr) {
        if (addr + sizeof(T) > memory.size()) [[unlikely]] {
            throw COMPILER_ERROR(fmt::format("Segfault: load from 0x{:x}", addr));
        }
        T val;
        __builtin_memcpy(&val, &memory[addr], sizeof(T));
        return val;
    }

    template <typename T> void store(uint64_t addr, T val) {
        if (addr + sizeof(T) > memory.size()) [[unlikely]] {
            throw COMPILER_ERROR(fmt::format("Segfault: store to 0x{:x}", addr));
        }
        __builtin_memcpy(&memory[addr], &val, sizeof(T));
    }
};

}  // namespace rv64::vm
