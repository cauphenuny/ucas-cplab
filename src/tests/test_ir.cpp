#include "backend/ir/ir.h"
#include "backend/ir/semantic.h"

#include <frontend/ast/ast.h>
#include <frontend/ast/op.h>
#include <utils/serialize.hpp>

int main() {
    using namespace ir;
    using namespace ast;

    std::vector<BlockStmt::Item> main_items;
    main_items.emplace_back(ReturnStmt{.exp = Exp{PrimaryExp{0}}});

    FuncDef main_def{
        .type = ast::Type::INT,
        .name = "main",
        .params = {},
        .block = BlockStmt{.items = std::move(main_items)},
    };

    std::vector<CompUnit::Item> unit_items;
    unit_items.emplace_back(std::move(main_def));
    auto comp_unit = std::make_unique<CompUnit>(Location{}, std::move(unit_items));

    SemanticAST info(std::move(comp_unit));

    Program program(info);

    ast::VarDef g_def_ast{.name = "g", .dims = {}, .val = std::nullopt};
    g_def_ast.loc = {0, 0};  // 假设全局变量定义在文件开头
    NamedValue g_named{.type = adt::construct<int>(), .def = &g_def_ast};
    program.globals.push_back(ir::Alloc{.var = g_named});

    ast::FuncDef foo_ast{.type = ast::Type::INT, .name = "foo", .params = {}, .block = {}};
    foo_ast.loc = {2, 1};

    Func foo{.type = adt::construct<int()>(), .name = "foo", .locals = {}, .blocks = {}};

    ast::VarDef local_ast{.name = "x", .dims = {}, .val = std::nullopt};
    local_ast.loc = {3, 1};
    NamedValue local_named{.type = adt::construct<int>(), .def = &local_ast};
    foo.locals.push_back(ir::Alloc{.var = local_named});

    TempValue t1{.type = adt::construct<int>(), .id = 1, .scope = nullptr};
    TempValue t2{.type = adt::construct<int>(), .id = 2, .scope = nullptr};

    std::vector<Inst> entry_insts;
    std::vector<Inst> then_insts;
    std::vector<Inst> else_insts;

    ConstexprValue c42{.type = adt::construct<int>(), .val = 42};
    ConstexprValue c1{.type = adt::construct<int>(), .val = 1};

    entry_insts.emplace_back(RegularInst{.op = InstOp::ADD, .result = t1, .lhs = c42, .rhs = c1});
    entry_insts.emplace_back(RegularInst{.op = InstOp::ADD, .result = t2, .lhs = t1, .rhs = c1});
    entry_insts.emplace_back(RegularInst{.op = InstOp::MUL, .result = t1, .lhs = t2, .rhs = c1});
    entry_insts.emplace_back(RegularInst{.op = InstOp::DIV, .result = t2, .lhs = t1, .rhs = c1});
    entry_insts.emplace_back(RegularInst{.op = InstOp::MOD, .result = t1, .lhs = t2, .rhs = c1});
    entry_insts.emplace_back(RegularInst{.op = InstOp::LT, .result = t2, .lhs = t1, .rhs = c42});
    entry_insts.emplace_back(RegularInst{.op = InstOp::EQ, .result = t1, .lhs = t2, .rhs = c1});

    then_insts.emplace_back(
        RegularInst{.op = InstOp::STORE, .result = g_named, .lhs = t1, .rhs = t2});

    else_insts.emplace_back(
        RegularInst{.op = InstOp::CALL, .result = t1, .lhs = local_named, .rhs = c42});
    else_insts.emplace_back(AggregateInst{.result = local_named, .src = {c42, t1}});

    foo.blocks.reserve(4);
    foo.blocks.push_back(Block{std::string(".entry"), std::move(entry_insts), JumpExit{nullptr}});
    foo.blocks.push_back(Block{std::string(".then"), std::move(then_insts), JumpExit{nullptr}});
    foo.blocks.push_back(Block{std::string(".else"), std::move(else_insts), JumpExit{nullptr}});
    foo.blocks.push_back(Block{std::string(".loop"), std::vector<Inst>{}, JumpExit{nullptr}});

    program.funcs.reserve(program.funcs.size() + 2);
    program.funcs.push_back(std::move(foo));

    Func& foo_ref = program.funcs.back();
    foo_ref.blocks[3].exit = JumpExit{&foo_ref.blocks[3]};
    foo_ref.blocks[1].exit = JumpExit{&foo_ref.blocks[3]};
    foo_ref.blocks[0].exit = JumpExit{&foo_ref.blocks[1]};
    foo_ref.blocks[2].exit = ReturnExit{t1};

    Func main_ir{.type = adt::construct<int()>(), .name = "main", .locals = {}, .blocks = {}};
    main_ir.blocks.push_back(
        Block{std::string(".entry"), std::vector<Inst>{}, JumpExit{&foo_ref.blocks[0]}});
    program.funcs.push_back(std::move(main_ir));
    program.entrance = &program.funcs.back();

    fmt::println("--- Complex IR Program ---");
    fmt::println("{}", program);
    return 0;
}
