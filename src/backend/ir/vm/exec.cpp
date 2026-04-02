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
                auto operand = lookup(unary.operand, frame);
                execute(unary, operand, ret);
            },
            [&](const BinaryInst& binary) {
                auto lhs = lookup(binary.lhs, frame);
                auto rhs = lookup(binary.rhs, frame);
                execute(binary, lhs, rhs, ret);
            },
            [&](const CallInst& call) {
                std::vector<View> srcs(call.args.size());
                for (const auto& arg : call.args) {
                    srcs.push_back(lookup(arg, frame));
                }
                execute(call, srcs, ret);
            });
    }
    auto exit = block.exit.value();
    return match(
        exit,
        [&](const BranchExit& branch) -> const Block* {
            View cond = lookup(branch.cond, frame);
            if (*(bool*)cond.data) {
                return branch.true_target;
            } else {
                return branch.false_target;
            }
        },
        [&](const JumpExit& jump) -> const Block* { return jump.target; },
        [&](const ReturnExit& ret_exit) -> const Block* {
            View exp = lookup(ret_exit.exp, frame);
            assign(ret, exp);
            return nullptr;
        });
}

void VirtualMachine::execute(const Func& func, const std::vector<View>& args, View& ret) const {

    size_t stack_size = 0;
    for (const auto& local : func.locals()) {
        stack_size += layout.size_of(local.var.type);
    }
    for (const auto& temp : func.temps()) {
        stack_size += layout.size_of(temp);
    }
    for (const auto& param : func.params) {
        stack_size += layout.size_of(param.type);
    }

    auto buffer = std::make_unique<std::byte[]>(stack_size);
    StackFrame frame;
    std::byte* cur = buffer.get();
    /// params
    for (size_t i = 0; i < func.params.size(); i++) {
        frame.vars[func.params[i]] = View{.data = cur, .type = func.params[i].type};
        assign(func.params[i].type, cur, args[i].type, args[i].data);
        cur += layout.size_of(func.params[i].type);
    }
    /// locals
    for (const auto& local : func.locals()) {
        frame.vars[local.var] = View{.data = cur, .type = local.var.type};
        cur += layout.size_of(local.var.type);
    }
    /// temps
    for (const auto& temp : func.temps()) {
        frame.temps.push_back(View{.data = cur, .type = temp});
        cur += layout.size_of(temp);
    }

    const Block* cur_block = func.blocks().front().get();
    while (cur_block) {
        cur_block = execute(*cur_block, frame, ret);
    }
}

}  // namespace ir::vm
