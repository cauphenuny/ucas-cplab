#include "backend/ir/ir.hpp"
#include "frontend/ast/ast.hpp"
#include "irgen.h"

namespace ir::gen {

auto Generator::gen(const ast::ConstInitVal* init, Type target_type) -> ConstexprValue {
    return match(
        init->val,
        [&](const ast::ConstExp& val) -> ConstexprValue {
            return match(val, [](auto v) { return ConstexprValue(v); });
        },
        [&](const std::vector<ast::ConstInitVal>& subvals) -> ConstexprValue {
            auto type = this->info->type_of(init).as<adt::Array>();
            if (!type.elem.is<adt::Array>() && target_type.as<adt::Array>().elem.is<adt::Array>()) {
                // NOTE: initialize multi-dim array with flat initializer list
                target_type = target_type.flatten();
            }
            auto buffer = std::make_unique<std::byte[]>(adt::size_of(target_type));
            memset(buffer.get(), 0, adt::size_of(target_type));

            auto construct = [&](auto self, const adt::Array& type, const adt::Array& target_type,
                                 const std::vector<ast::ConstInitVal>& elems,
                                 std::byte* buf) -> void {
                // NOTE: when array is not fully initialized, elems.size() < type.size
                auto length = elems.size();
                auto elem_size = adt::size_of(target_type.elem);
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
                            self(self, type.elem.as<adt::Array>(),
                                 target_type.elem.as<adt::Array>(), subvals, buf);
                        });
                    buf += elem_size;
                }
            };

            construct(construct, type, target_type.as<adt::Array>(), subvals, buffer.get());

            return {target_type.flatten(), std::move(buffer)};
        });
}

auto Generator::gen(const ast::ConstDef* def) -> std::unique_ptr<Alloc> {
    auto type = this->info->type_of(def);
    auto alloc = std::make_unique<Alloc>(this->name_of(def), type, gen(&def->val, type));
    this->ir_defs[def] = alloc.get();
    return alloc;
}

auto Generator::gen(const ast::VarDef* def) -> std::unique_ptr<Alloc> {
    auto type = this->info->type_of(def);
    auto alloc = std::make_unique<Alloc>(this->name_of(def), type,
                                         def->val ? std::make_optional(gen(&*def->val, type))
                                                  : std::nullopt);
    this->ir_defs[def] = alloc.get();
    return alloc;
}

auto Generator::gen(const ast::FuncParam* param) -> std::unique_ptr<Alloc> {
    auto type = this->info->type_of(param);
    auto alloc = std::make_unique<Alloc>(this->name_of(param), type);
    this->ir_defs[param] = alloc.get();
    return alloc;
}

auto Generator::gen(const ast::Decl* decl) -> std::vector<std::unique_ptr<Alloc>> {
    return match(*decl, [&](const auto& decl) {
        std::vector<std::unique_ptr<Alloc>> allocs;
        for (const auto& def : decl.defs) {
            auto var_alloc = gen(&def);
            allocs.push_back(std::move(var_alloc));
        }
        return allocs;
    });
}

auto Generator::gen(const ast::FuncDef* func) -> std::unique_ptr<Func> {
    auto type = this->info->type_of(func).as<adt::Func>();
    auto params = std::vector<std::unique_ptr<Alloc>>{};
    for (const auto& param : func->params) {
        auto alloc = gen(&param);
        params.push_back(std::move(alloc));
    }
    auto ir_func = std::make_unique<Func>(type.ret, this->name_of(func), std::move(params));
    ir_func->newBlock("entry");
    this->ir_defs[func] = ir_func.get();
    auto end = gen(&func->block, ir_func.get(), ir_func->entrance());
    if (end) {
        if (!(type.ret <= adt::construct<void>())) {
            throw COMPILER_ERROR(fmt::format("control may reach end of function '{}'", func->name));
        } else {
            end->setExit(ReturnExit{});
        }
    }
    return ir_func;
}

auto Generator::generate(const ast::SemanticAST& info) -> Program {
    this->info = &info;
    this->ast = &info.ast();

    auto prog = Program();

    this->ir_defs = std::unordered_map<ast::SymDefNode, ir::NameDef>{};
    for (const auto& func : ast::BUILTIN_FUNCS) {
        auto type = info.type_of(&func);
        auto ir_func = std::make_unique<BuiltinFunc>(this->name_of(&func), type);
        this->ir_defs[&func] = ir_func.get();
        prog.addBuiltinFunc(std::move(ir_func));
    }

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