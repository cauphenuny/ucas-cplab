#include "frontend/ast/op.h"

#include <frontend/ast/ast.h>
#include <optional>
#include <utils/serialize.hpp>

int main() {
    using namespace ast;

    // 1. Constant Expression
    fmt::println("--- Constant ---");
    fmt::println("{}", Exp{PrimaryExp{3.14159}});

    // 2. Binary Expression: 1 + 2 * 3
    fmt::println("\n--- Binary: 1 + 2 * 3 ---");
    auto mul = BinaryExp{
        .op = BinaryOp::MUL, .left = PrimaryExp(2).toBoxed(), .right = PrimaryExp(3).toBoxed()};
    auto add = BinaryExp{
        .op = BinaryOp::ADD, .left = PrimaryExp(1).toBoxed(), .right = std::move(mul).toBoxed()};
    fmt::println("{}", add);

    // 3. Function Call: putint(42, 2 + 3)
    fmt::println("\n--- Function Call: putint(42, 2 + 3) ---");
    std::vector<Exp> call_args;
    call_args.emplace_back(PrimaryExp{42});
    call_args.emplace_back(BinaryExp{
        .op = BinaryOp::ADD, .left = PrimaryExp{2}.toBoxed(), .right = PrimaryExp{3}.toBoxed()});
    auto call = BinaryExp{.op = BinaryOp::CALL,
                          .left = PrimaryExp{LValExp{LVal{.name = "putint"}}}.toBoxed(),
                          .right = TupleExp{.elements = std::move(call_args)}.toBoxed()};
    fmt::println("{}", call);

    // 4. If Statement: if (x > 0) return 1; else return 0;
    fmt::println("\n--- If Statement ---");
    auto x_gt_0 = Exp{BinaryExp{.op = BinaryOp::GT,
                                .left = PrimaryExp{LValExp{LVal{.name = "x"}}}.toBoxed(),
                                .right = PrimaryExp{0}.toBoxed()}};

    auto ret1 = std::make_unique<Stmt>(Stmt{ReturnStmt{.exp = Exp{PrimaryExp{1}}}});
    auto ret0 = std::make_unique<Stmt>(Stmt{ReturnStmt{.exp = Exp{PrimaryExp{0}}}});

    auto if_stmt = Stmt{
        IfStmt{.cond = std::move(x_gt_0), .stmt = std::move(ret1), .else_stmt = std::move(ret0)}};
    fmt::println("{}", if_stmt);

    // 5. Unary Expression: -(!x)
    fmt::println("\n--- Unary: -(!x) ---");
    auto not_x =
        UnaryExp{.op = UnaryOp::NOT, .exp = PrimaryExp{LValExp{LVal{.name = "x"}}}.toBoxed()};
    auto neg_not_x = UnaryExp{.op = UnaryOp::MINUS, .exp = std::move(not_x).toBoxed()};
    fmt::println("{}", neg_not_x);

    // 6. While Loop: while (i < 10) i = i + 1;
    fmt::println("\n--- While Loop ---");
    auto cond = Exp{BinaryExp{.op = BinaryOp::LT,
                              .left = PrimaryExp{LValExp{LVal{.name = "i"}}}.toBoxed(),
                              .right = PrimaryExp{10}.toBoxed()}};
    auto assign =
        AssignStmt{.var = LValExp{LVal{.name = "i"}},
                   .exp = Exp{BinaryExp{.op = BinaryOp::ADD,
                                        .left = PrimaryExp{LValExp{LVal{.name = "i"}}}.toBoxed(),
                                        .right = PrimaryExp{1}.toBoxed()}}};
    auto while_stmt = WhileStmt{.cond = std::move(cond), .stmt = std::move(assign).toBoxed()};
    fmt::println("{}", while_stmt);

    // 7. Complex: Function Definition and Block
    // int main() { int x = 1; return x; }
    fmt::println("\n--- Function Definition ---");
    std::vector<VarDef> var_defs;
    var_defs.push_back(VarDef{.name = "x", .dims = {}, .val = ConstInitVal{.val = ConstExp{1}}});
    auto var_decl = Decl{VarDecl{.type = Type::INT, .defs = std::move(var_defs)}};

    std::vector<BlockStmt::Item> items;
    items.emplace_back(std::move(var_decl));
    items.emplace_back(ReturnStmt{.exp = Exp{PrimaryExp{LValExp{LVal{.name = "x"}}}}});

    auto main_func = FuncDef{.type = Type::INT,
                             .name = "main",
                             .params = {},
                             .block = BlockStmt{.items = std::move(items)}};
    fmt::println("{}", main_func);

    // 8. Array Access and Multi-dimensional Array
    // a[1][i + 2] = 42;
    fmt::println("\n--- Array Access ---");
    std::vector<ExpBox> indices;
    indices.emplace_back(PrimaryExp{1}.toBoxed());
    indices.emplace_back(BinaryExp{.op = BinaryOp::ADD,
                                   .left = PrimaryExp{LValExp{LVal{.name = "i"}}}.toBoxed(),
                                   .right = PrimaryExp{2}.toBoxed()}
                             .toBoxed());

    auto array_assign = AssignStmt{
        .var = LValExp{BinaryExp{
            .op = BinaryOp::INDEX,
            .left = PrimaryExp{LValExp{BinaryExp{
                                   .op = BinaryOp::INDEX,
                                   .left = PrimaryExp{LValExp{LVal{.name = "a"}}}.toBoxed(),
                                   .right = std::move(indices[1])}}}
                        .toBoxed(),
            .right = std::move(indices[0])}},
        .exp = Exp{PrimaryExp{42}}};
    fmt::println("{}", array_assign);

    // 9. CompUnit: Full Module Example
    /*
        const int N = 100;
        int a[100];
        int fib(int n) {
            if (n <= 1) return n;
            return fib(n-1) + fib(n-2);
        }
        int main() {
            return fib(10);
        }
    */
    fmt::println("\n--- CompUnit: Full Module ---");
    std::vector<CompUnit::Item> unit_items;

    // const int N = 100;
    std::vector<ConstDef> n_defs;
    n_defs.push_back(ConstDef{.name = "N", .dims = {}, .val = ConstInitVal{.val = ConstExp{100}}});
    unit_items.emplace_back(Decl{ConstDecl{.type = Type::INT, .defs = std::move(n_defs)}});

    // int a[100];
    std::vector<VarDef> a_defs;
    a_defs.push_back(VarDef{.name = "a", .dims = {100}, .val = std::nullopt});
    unit_items.emplace_back(Decl{VarDecl{.type = Type::INT, .defs = std::move(a_defs)}});

    // int fib(int n) { ... }
    {
        FuncParams params;
        params.push_back(FuncParam{.type = Type::INT, .name = "n", .dims = {}});

        std::vector<BlockStmt::Item> fib_items;
        // if (n <= 1) return n;
        auto if_cond = Exp{BinaryExp{.op = BinaryOp::LEQ,
                                     .left = PrimaryExp{LValExp{LVal{.name = "n"}}}.toBoxed(),
                                     .right = PrimaryExp{1}.toBoxed()}};
        auto ret_n = ReturnStmt{.exp = Exp{PrimaryExp{LValExp{LVal{.name = "n"}}}}};
        fib_items.emplace_back(IfStmt{.cond = std::move(if_cond),
                                      .stmt = std::move(ret_n).toBoxed(),
                                      .else_stmt = std::nullopt});

        // return fib(n-1) + fib(n-2);
        auto n_minus_1 = BinaryExp{.op = BinaryOp::SUB,
                                   .left = PrimaryExp{LValExp{LVal{.name = "n"}}}.toBoxed(),
                                   .right = PrimaryExp{1}.toBoxed()};
        auto n_minus_2 = BinaryExp{.op = BinaryOp::SUB,
                                   .left = PrimaryExp{LValExp{LVal{.name = "n"}}}.toBoxed(),
                                   .right = PrimaryExp{2}.toBoxed()};

        std::vector<Exp> args1;
        args1.emplace_back(std::move(n_minus_1));
        std::vector<Exp> args2;
        args2.emplace_back(std::move(n_minus_2));

        auto call1 = BinaryExp{.op = BinaryOp::CALL,
                               .left = PrimaryExp{LValExp{LVal{.name = "fib"}}}.toBoxed(),
                               .right = TupleExp{.elements = std::move(args1)}.toBoxed()};
        auto call2 = BinaryExp{.op = BinaryOp::CALL,
                               .left = PrimaryExp{LValExp{LVal{.name = "fib"}}}.toBoxed(),
                               .right = TupleExp{.elements = std::move(args2)}.toBoxed()};

        auto add_fib = BinaryExp{.op = BinaryOp::ADD,
                                 .left = std::move(call1).toBoxed(),
                                 .right = std::move(call2).toBoxed()};
        fib_items.emplace_back(ReturnStmt{.exp = Exp{std::move(add_fib)}});

        unit_items.emplace_back(FuncDef{.type = Type::INT,
                                        .name = "fib",
                                        .params = std::move(params),
                                        .block = BlockStmt{.items = std::move(fib_items)}});
    }

    // int main() { return fib(10); }
    {
        std::vector<BlockStmt::Item> main_items;
        std::vector<Exp> fib_args;
        fib_args.emplace_back(PrimaryExp{10});
        auto fib_call = BinaryExp{.op = BinaryOp::CALL,
                                  .left = PrimaryExp{LValExp{LVal{.name = "fib"}}}.toBoxed(),
                                  .right = TupleExp{.elements = std::move(fib_args)}.toBoxed()};
        main_items.emplace_back(ReturnStmt{.exp = Exp{std::move(fib_call)}});

        unit_items.emplace_back(FuncDef{.type = Type::INT,
                                        .name = "main",
                                        .params = {},
                                        .block = BlockStmt{.items = std::move(main_items)}});
    }

    auto comp_unit = CompUnit{Location{}, std::move(unit_items)};
    fmt::println("{}", comp_unit);

    // 10. Location Verification
    fmt::println("\n--- Location Verification ---");
    LVal x_loc{.name = "x"};
    x_loc.loc = {10, 5};
    fmt::println("LValExp with location: {}", x_loc);

    BinaryExp add_loc{
        .op = BinaryOp::ADD, .left = PrimaryExp(2).toBoxed(), .right = PrimaryExp(3).toBoxed()};
    add_loc.loc = {11, 1};
    fmt::println("BinaryExp with location: {}", add_loc);

    return 0;
}