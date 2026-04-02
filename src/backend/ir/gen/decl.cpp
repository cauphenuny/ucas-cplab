#include "backend/ir/ir.hpp"
#include "frontend/ast/ast.hpp"
#include "irgen.h"

namespace ir::gen {

auto Generator::gen(const ast::ConstInitVal* init) -> ConstexprValue {
    return match(
        init->val,
        [&](const ast::ConstExp& val) -> ConstexprValue {
            return match(val, [](auto v) { return ConstexprValue(v); });
        },
        [&](const std::vector<ast::ConstInitVal>& subvals) -> ConstexprValue {
            auto type = this->info->type_of(init).as<adt::Array>();
            auto buffer = std::make_unique<std::byte[]>(adt::size_of(type));
            memset(buffer.get(), 0, adt::size_of(type));

            auto construct = [&](auto self, const adt::Array& type,
                                 const std::vector<ast::ConstInitVal>& elems,
                                 std::byte* buf) -> void {
                auto length = type.size;
                auto elem_size = adt::size_of(type.elem);
                for (size_t i = 0; i < length; i++) {
                    auto& elem = elems[i];
                    match(
                        elem.val,
                        [&](const ast::ConstExp& val) {
                            match(val, [&](auto v) {
                                using type = decltype(v);
                                *(type*)buf = v;
                            });
                        },
                        [&](const std::vector<ast::ConstInitVal>& subvals) {
                            self(self, type.elem.as<adt::Array>(), subvals, buf);
                        });
                    buf += elem_size;
                }
            };

            construct(construct, type, subvals, buffer.get());

            return {std::move(type).toBoxed(), std::move(buffer)};
        });
}

auto Generator::gen(const ast::ConstDef* def) -> Alloc {
    return Alloc{NamedValue{this->info->type_of(def), def}, gen(&def->val)};
}

auto Generator::gen(const ast::VarDef* def) -> Alloc {
    return Alloc{NamedValue{this->info->type_of(def), def},
                 def->val ? std::make_optional(gen(&*def->val)) : std::nullopt};
}

auto Generator::gen(const ast::Decl* decl) -> std::vector<Alloc> {
    return match(*decl, [&](const auto& decl) {
        std::vector<Alloc> allocs;
        for (const auto& def : decl.defs) {
            auto var_alloc = gen(&def);
            allocs.push_back(std::move(var_alloc));
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