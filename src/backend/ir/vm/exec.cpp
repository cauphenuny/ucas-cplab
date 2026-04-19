#include "backend/ir/ir.h"
#include "backend/ir/type.hpp"
#include "backend/ir/vm/view.hpp"
#include "utils/diagnosis.hpp"
#include "utils/match.hpp"
#include "vm.h"

#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ir::vm {

void VirtualMachine::execute(const BinaryInst& inst, const View& lhs, const View& rhs, View& ret) {
    eval(inst.op, ret, lhs, rhs);
}

void VirtualMachine::execute(const UnaryInst& inst, const View& operand, View& ret) {
    eval(inst.op, ret, operand);
}

void VirtualMachine::execute(const CallInst& inst, const std::vector<View>& srcs, View& ret) {
    auto func = inst.func;
    auto def = func.def;
    match(
        def, [&](const ir::Func* func) { return execute(*func, srcs, ret); },
        [&](const ir::BuiltinFunc* builtin_func) {
            auto vm_func = BUILTIN_FUNCS.find(builtin_func->name);
            if (vm_func == BUILTIN_FUNCS.end()) {
                throw COMPILER_ERROR(
                    fmt::format("Builtin function '{}' not found", builtin_func->name));
            }
            return execute(vm_func->second, srcs, ret);
        },
        [&](const ir::Alloc*) {
            throw COMPILER_ERROR(fmt::format("Cannot call variable {}", func));
        });
}

void VirtualMachine::execute(const BuiltinFunc& func, const std::vector<View>& args, View& ret) {
    func.apply(ret, args, input, output);
}

auto VirtualMachine::execute(Block& block, Block* prev, StackFrame& frame, View& ret) -> Block* {
    struct PhiUpdate {
        View dest;
        std::vector<std::byte> data;
    };
    std::vector<PhiUpdate> phi_updates;

    auto it = block.insts().begin();
    while (it != block.insts().end()) {
        if (auto phi = std::get_if<PhiInst>(&*it)) {
            auto operand = view_of(phi->args.at(prev), frame);
            auto dest = view_of(phi->result, frame);
            size_t size = ir::type::size_of(dest.type);
            std::vector<std::byte> buffer(size);
            memcpy(buffer.data(), operand.data, size);
            phi_updates.push_back({dest, std::move(buffer)});
            it++;
            perf_counter.num_insts++;
        } else {
            break;
        }
    }

    for (auto& update : phi_updates) {
        View src_buffer_view{.data = update.data.data(), .type = update.dest.type};
        assign(update.dest, src_buffer_view);
    }

    while (it != block.insts().end()) {
        const auto& inst = *it;
        match(
            inst,
            [&](const UnaryInst& unary) {
                auto operand = view_of(unary.operand, frame);
                auto result = view_of(unary.result, frame);
                execute(unary, operand, result);
            },
            [&](const BinaryInst& binary) {
                auto lhs = view_of(binary.lhs, frame);
                auto rhs = view_of(binary.rhs, frame);
                auto result = view_of(binary.result, frame);
                execute(binary, lhs, rhs, result);
            },
            [&](const CallInst& call) {
                auto result = view_of(call.result, frame);
                std::vector<View> srcs;
                srcs.reserve(call.args.size());
                for (const auto& arg : call.args) {
                    srcs.push_back(view_of(arg, frame));
                }
                execute(call, srcs, result);
            },
            [&](const PhiInst& phi) {
                throw COMPILER_ERROR("Phi instruction must be at the beginning of a block");
            });
        perf_counter.num_insts++;
        it++;
    }
    auto& exit = block.exit();
    perf_counter.num_insts++;  // count exit instruction as well
    return match(
        exit,
        [&](const BranchExit& branch) -> Block* {
            View cond = view_of(branch.cond, frame);
            if (*(bool*)cond.data) {
                return branch.true_target;
            } else {
                return branch.false_target;
            }
        },
        [&](const JumpExit& jump) -> Block* { return jump.target; },
        [&](const ReturnExit& ret_exit) -> Block* {
            View exp = view_of(ret_exit.exp, frame);
            assign(ret, exp);
            return nullptr;
        });
}

void VirtualMachine::alloc(StackFrame& frame, Alloc* alloc, std::byte* buffer) const {
    if (alloc->reference) {
        // buffer layout: [actual data (size_of(type)) | ptr (sizeof(std::byte*))]
        auto ref_type = alloc->type.borrow(alloc->immutable);
        auto ptr = buffer + ir::type::size_of(alloc->type);
        *(std::byte**)ptr = buffer;
        frame.vars[alloc] = View{.data = ptr, .type = ref_type};
    } else {
        frame.vars[alloc] = View{.data = buffer, .type = alloc->type};
    }
    if (alloc->init) {
        auto init_val = view_of(*alloc->init);
        assign(alloc->type, buffer, init_val.type, init_val.data);
    }
}

size_t VirtualMachine::stackSize(const std::unique_ptr<Alloc>& alloc) const {
    return ir::type::size_of(alloc->type) + (alloc->reference ? sizeof(void*) : 0);
}

size_t VirtualMachine::stackSize(const Type& type) const {
    return ir::type::size_of(type);
}

void VirtualMachine::execute(const Func& func, const std::vector<View>& args, View& ret) {

    size_t stack_size = 0;
    for (const auto& local : func.locals()) {
        stack_size += stackSize(local);
    }
    for (const auto& temp : func.temps()) {
        stack_size += stackSize(temp.type);
    }
    for (const auto& param : func.params) {
        stack_size += stackSize(param);
    }

    auto buffer = std::make_unique<std::byte[]>(stack_size);
    memset(buffer.get(), 0, stack_size);
    StackFrame frame;
    std::byte* cur = buffer.get();
    /// params
    if (func.params.size() != args.size()) {
        throw COMPILER_ERROR(
            fmt::format("Argument count mismatch in call to {}: expected {}, got {}", func.name,
                        func.params.size(), args.size()));
    }
    for (size_t i = 0; i < func.params.size(); i++) {
        auto& param = func.params[i];
        frame.vars[param.get()] = View{.data = cur, .type = param->type};
        assign(param->type, cur, args[i].type, args[i].data);
        cur += stackSize(param);
    }
    /// locals
    for (const auto& local : func.locals()) {
        alloc(frame, local.get(), cur);
        cur += stackSize(local);
    }
    /// temps
    for (const auto& temp : func.temps()) {
        frame.temps.push_back(View{.data = cur, .type = temp.type});
        cur += stackSize(temp.type);
    }

    Block *cur_block = func.entrance(), *prev = nullptr;
    while (cur_block) {
        auto next = execute(*cur_block, prev, frame, ret);
        prev = cur_block;
        cur_block = next;
    }
}

uint8_t VirtualMachine::execute(const Program& program) {
    this->program = &program;
    this->global_frame = StackFrame();
    this->perf_counter = {};

    size_t global_size = 0;
    /// global variables
    for (const auto& global : program.globals) {
        global_size += stackSize(global);
    }
    /// return value
    global_size += sizeof(int);

    auto buffer = std::make_unique<std::byte[]>(global_size);
    memset(buffer.get(), 0, global_size);
    std::byte* cur = buffer.get();
    for (const auto& global : program.globals) {
        alloc(global_frame, global.get(), cur);
        cur += stackSize(global);
    }

    View ret{.data = cur, .type = ir::type::construct<int>()};

    auto& main_func = program.findFunc("main");
    std::vector<View> args;
    execute(main_func, args, ret);
    return *(int*)ret.data;
}

}  // namespace ir::vm
