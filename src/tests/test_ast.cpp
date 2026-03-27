#define FMT_HEADER_ONLY
#include <fmt/format.h>
#include <frontend/ast/ast.h>
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

    // 3. Function Call: putint(42)
    fmt::println("\n--- Function Call: putint(42, 2 + 3) ---");
    std::vector<Exp> args;
    args.emplace_back(PrimaryExp(42));
    args.emplace_back(BinaryExp{
        .op = BinaryOp::ADD, .left = PrimaryExp{2}.toBoxed(), .right = PrimaryExp{3}.toBoxed()});
    auto call = CallExp{.name = "putint", .args = FuncArgs{.args = std::move(args)}};
    fmt::println("{}", call);

    // 4. If Statement: if (x > 0) return 1; else return 0;
    fmt::println("\n--- If Statement ---");
    auto x_gt_0 = Exp{BinaryExp{.op = BinaryOp::GT,
                                .left = PrimaryExp{LeftVal{.name = "x"}}.toBoxed(),
                                .right = PrimaryExp{0}.toBoxed()}};

    auto ret1 = std::make_unique<Stmt>(Stmt{ReturnStmt{.exp = Exp{PrimaryExp{1}}}});
    auto ret0 = std::make_unique<Stmt>(Stmt{ReturnStmt{.exp = Exp{PrimaryExp{0}}}});

    auto if_stmt = Stmt{
        IfStmt{.cond = std::move(x_gt_0), .stmt = std::move(ret1), .else_stmt = std::move(ret0)}};
    fmt::println("{}", if_stmt);

    return 0;
}