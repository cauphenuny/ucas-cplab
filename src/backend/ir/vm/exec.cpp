#include "backend/ir/ir.hpp"
#include "vm.h"

namespace ir::vm {

void VirtualMachine::execute(const BinaryInst& inst, const View& lhs, const View& rhs,
                             View& ret) const {
    eval(inst.op, ret, lhs, rhs);
}

void VirtualMachine::execute(const UnaryInst& inst, const View& operand, View& ret) const {
    eval(inst.op, ret, operand);
}

void VirtualMachine::execute(const CallInst& inst, const std::vector<View>& srcs, View& ret) const {
    auto func = inst.func;
    auto def = func.def;
    auto name = match(def, [&](const auto* d) { return d->name; });
    auto [_, is_builtin] = this->ast->funcs.at(name);
    if (is_builtin) {
        return execute(builtin_funcs.at(name), srcs, ret);
    } else {
        return execute(this->program->findFunc(name), srcs, ret);
    }
}

void VirtualMachine::execute(const BuiltinFunc& func, const std::vector<View>& args,
                             View& ret) const {
    func.apply(ret, args, input, output);
}

auto VirtualMachine::execute(const Block& block, StackFrame& frame, View& ret) const
    -> const Block* {
    for (const auto& inst : block.insts) {
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
            });
    }
    if (!block.exit) {
        throw CompilerError(fmt::format("Block {} has no exit instruction", block.label));
    }
    auto& exit = block.exit.value();
    return match(
        exit,
        [&](const BranchExit& branch) -> const Block* {
            View cond = view_of(branch.cond, frame);
            if (*(bool*)cond.data) {
                return branch.true_target;
            } else {
                return branch.false_target;
            }
        },
        [&](const JumpExit& jump) -> const Block* { return jump.target; },
        [&](const ReturnExit& ret_exit) -> const Block* {
            View exp = view_of(ret_exit.exp, frame);
            assign(ret, exp);
            return nullptr;
        });
}

void VirtualMachine::alloc(StackFrame& frame, const Alloc& alloc, std::byte* buffer) const {
    if (frame.vars.count(alloc.var)) {
        throw CompilerError(fmt::format("Variable {} already defined in this scope", alloc.var));
    }
    frame.vars[alloc.var] = View{.data = buffer, .type = alloc.var.type};
    if (alloc.init) {
        auto init_val = view_of(*alloc.init);
        assign(alloc.var.type, frame.vars[alloc.var].data, init_val.type, init_val.data);
    } else {
        memset(frame.vars[alloc.var].data, 0, adt::size_of(alloc.var.type));
    }
}

void VirtualMachine::execute(const Func& func, const std::vector<View>& args, View& ret) const {

    size_t stack_size = 0;
    for (const auto& local : func.locals()) {
        stack_size += adt::size_of(local.var.type);
    }
    for (const auto& temp : func.temps()) {
        stack_size += adt::size_of(temp);
    }
    for (const auto& param : func.params) {
        stack_size += adt::size_of(param.type);
    }

    auto buffer = std::make_unique<std::byte[]>(stack_size);
    StackFrame frame;
    std::byte* cur = buffer.get();
    /// params
    if (func.params.size() != args.size()) {
        throw CompilerError(
            fmt::format("Argument count mismatch in call to {}: expected {}, got {}", func.name,
                        func.params.size(), args.size()));
    }
    for (size_t i = 0; i < func.params.size(); i++) {
        frame.vars[func.params[i]] = View{.data = cur, .type = func.params[i].type};
        assign(func.params[i].type, cur, args[i].type, args[i].data);
        cur += adt::size_of(func.params[i].type);
    }
    /// locals
    for (const auto& local : func.locals()) {
        alloc(frame, local, cur);
        cur += adt::size_of(local.var.type);
    }
    /// temps
    for (const auto& temp : func.temps()) {
        frame.temps.push_back(View{.data = cur, .type = temp});
        cur += adt::size_of(temp);
    }

    const Block* cur_block = func.blocks().front().get();
    while (cur_block) {
        cur_block = execute(*cur_block, frame, ret);
    }
}

int VirtualMachine::execute(const Program& program) {
    this->ast = &program.ast;
    this->program = &program;
    this->global_frame = StackFrame();

    size_t global_size = 0;
    /// global variables
    for (const auto& global : program.globals) {
        global_size += adt::size_of(global.var.type);
    }
    /// return value
    global_size += sizeof(int);

    auto buffer = std::make_unique<std::byte[]>(global_size);
    std::byte* cur = buffer.get();
    for (const auto& global : program.globals) {
        alloc(global_frame, global, cur);
        cur += adt::size_of(global.var.type);
    }

    View ret{.data = cur, .type = adt::construct<int>()};

    auto& main_func = program.findFunc("main");
    std::vector<View> args;
    execute(main_func, args, ret);
    return *(int*)ret.data;
}

}  // namespace ir::vm
