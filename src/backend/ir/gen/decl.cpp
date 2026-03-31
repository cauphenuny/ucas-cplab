#include "backend/ir/ir.hpp"
#include "frontend/ast/ast.hpp"
#include "irgen.h"

namespace ir::gen {

auto Generator::gen(const ast::ConstDef* def) -> Alloc {
    return Alloc{NamedValue{this->info->type_of(def), def}, &def->val};
}

auto Generator::gen(const ast::VarDef* def) -> Alloc {
    return Alloc{NamedValue{this->info->type_of(def), def}, def->val ? &*def->val : nullptr};
}

auto Generator::gen(const ast::Decl* decl) -> std::vector<Alloc> {
    return match(*decl, [&](const auto& decl) {
        std::vector<Alloc> allocs;
        for (const auto& def : decl.defs) {
            auto var_alloc = gen(&def);
            allocs.push_back(var_alloc);
        }
        return allocs;
    });
}

auto Generator::gen(const ast::FuncDef* func) -> Func {
    auto type = this->info->type_of(func).as<adt::Func>();
    auto params = std::vector<NamedValue>{};
    for (const auto& param : func->params) {
        auto param_type = this->info->type_of(&param);
        params.emplace_back(NamedValue{param_type, &param});
    }
    auto ir_func = Func(type.ret, func->name, std::move(params));
    auto end = gen(&func->block, &ir_func, ir_func.entrance());
    if (end) {
        if (!(type.ret <= adt::construct<void>())) {
            throw CompilerError(fmt::format("control may reach end of function '{}'", func->name));
        } else {
            end->exit = ReturnExit{};
        }
    }
    return ir_func;
}

auto Generator::generate(const ast::SemanticAST& info) -> Program {
    this->info = &info;
    this->ast = &info.ast();
    auto prog = Program(info);
    for (const auto& item : ast->items) {
        match(
            item,
            [&](const ast::Decl& decl) {
                for (auto&& alloc : gen(&decl)) {
                    prog.addGlobal(std::move(alloc));
                }
            },
            [&](const ast::FuncDef& func) {
                auto f = gen(&func);
                prog.addFunc(std::move(f));
            });
    }
    return prog;
}

}  // namespace ir::gen