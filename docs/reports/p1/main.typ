#import "../preamble/preamble.typ": *

#show: doc => conf(title: "P1 实验报告", doc)

= 整体设计

本实验的目标是基于 ANTLR 工具完成 CACT 语言的词法分析与语法分析，并在此基础上构建抽象语法树（AST）。整体架构分为三层：

+ *词法/语法层*：编写 ANTLR 文法文件 `CACT.g4`，由 ANTLR 自动生成词法分析器（`CACTLexer`）和语法分析器（`CACTParser`），完成源码到解析树（Parse Tree）的转换。
+ *AST 构建层*：实现 `ASTVisitor`（继承 `CACTBaseVisitor`），遍历 ANTLR 生成的解析树，将其转换为自定义的、结构化的 AST 节点体系。
+ *语义分析层*：在 AST 上实现 `SemanticAST`，完成类型推断、作用域管理、符号定义与引用解析等语义检查。

整体流水线为：

```
CACT 源码 → CACTLexer → CACTParser → Parse Tree → ASTVisitor → AST → SemanticAST
```

== 语法设计

=== 文法文件总体结构

文法文件 `CACT.g4` 采用 ANTLR 4 的 combined grammar 格式，将词法规则（lexer rules）和语法规则（parser rules）统一编写在一个文件中。词法规则使用大写字母开头的标识符定义，语法规则使用小写字母开头。

=== 词法规则设计

词法规则按以下顺序和优先级排列：

*1. 关键字*。关键字排在标识符规则 `ID` 之前，利用 ANTLR 的"先定义者优先匹配"策略，确保 `continue`、`return` 等保留字不会被错误地匹配为标识符：

```
TRUE: 'true';    FALSE: 'false';   CONST: 'const';
INT: 'int';      BOOL: 'bool';     FLOAT: 'float';
DOUBLE: 'double'; VOID: 'void';    IF: 'if';
ELSE: 'else';    WHILE: 'while';   BREAK: 'break';
CONTINUE: 'continue'; RETURN: 'return';
```

*2. 数值常量*。整数字面量支持十进制、八进制和十六进制三种形式；浮点字面量通过 `fragment FLOAT_NUMBER` 统一定义浮点数的基本格式，再由后缀区分 `float`（带 `f/F` 后缀）和 `double`（无后缀）：

```antlr
INT_LITERAL:
    [1-9] DIGIT*             // 十进制：非零开头
    | '0' DIGIT_OCT*         // 八进制：零开头
    | ('0x' | '0X') DIGIT_HEX+  // 十六进制
    ;
fragment FLOAT_NUMBER
    : DIGIT+ '.' DIGIT* ([eE] [+-]? DIGIT+)?
    | '.' DIGIT+ ([eE] [+-]? DIGIT+)?
    | DIGIT+ [eE] [+-]? DIGIT+
    ;
FLOAT_LITERAL: FLOAT_NUMBER [fF];
DOUBLE_LITERAL: FLOAT_NUMBER;
```

*3. 标识符*。`ID` 规则匹配以字母或下划线开头、后跟字母/数字/下划线的字符序列：

```antlr
ID: [a-zA-Z_][a-zA-Z0-9_]*;
```

*4. 注释和空白*。使用 `-> skip` 指令将注释和空白字符从 Token 流中丢弃：

```antlr
LINE_COMMENT: '//' ~[\r\n]* -> skip;
BLOCK_COMMENT: '/*' .*? '*/' -> skip;
WS: [ \t\r\n]+ -> skip;
```

其中块注释使用 `.*?`（非贪婪模式）确保匹配到最近的 `*/` 即停止。

=== 语法规则设计

*1. 程序顶层结构*。编译单元 `compUnit` 由零或多个声明（`decl`）与函数定义（`funcDef`）组成：

```antlr
compUnit: (decl | funcDef)* EOF;
decl: constDecl | varDecl;
```

*2. 声明与定义*。常量声明和变量声明均支持多变量同时定义与多维数组：

```antlr
constDecl: CONST basicType constDef (',' constDef)* ';';
constDef: ID ('[' INT_LITERAL ']')* '=' constInitVal;
varDecl: basicType varDef (',' varDef)* ';';
varDef: ID ('[' INT_LITERAL ']')* ('=' constInitVal)?;
```

常量初始化值 `constInitVal` 支持递归嵌套的花括号列表，以支持多维数组的初始化：

```antlr
constInitVal: constExp | '{' (constInitVal (',' constInitVal)*)? '}';
```

*3. 函数定义*。函数参数支持数组类型传参（首维可省略大小）：

```antlr
funcDef: funcType ID '(' funcParams? ')' block;
funcParam: basicType ID ('[' INT_LITERAL? ']' ('[' INT_LITERAL ']')*)?;
```

*4. 表达式优先级与结合性*。表达式部分通过多层递归规则体现运算符优先级——低优先级在上层，高优先级在下层：

```antlr
lOrExp  : lAndExp       | lOrExp  ('||' lAndExp);    // 最低
lAndExp : eqExp         | lAndExp ('&&' eqExp);
eqExp   : relExp        | eqExp   ('==' | '!=') relExp;
relExp  : addExp        | relExp  ('<' | '>' | '<=' | '>=') addExp;
addExp  : mulExp        | addExp  ('+' | '-') mulExp;
mulExp  : unaryExp      | mulExp  ('*' | '/' | '%') unaryExp;
unaryExp: ('+' | '-' | '!') unaryExp | primaryExp
        | ID '(' funcArgs? ')';                       // 最高
```

左递归规则（如 `addExp: addExp ('+' | '-') mulExp`）天然表达了左结合性，ANTLR 4 能够正确处理直接左递归。各运算符优先级从低到高为：逻辑或 → 逻辑与 → 等值比较 → 关系比较 → 加减 → 乘除模 → 一元运算/函数调用。

*5. 语句*。`stmt` 规则涵盖赋值、表达式语句、块语句、条件分支、循环、跳转和返回等所有语句类型：

```antlr
stmt: lVal '=' exp ';' | exp? ';' | block
    | IF '(' cond ')' stmt (ELSE stmt)?
    | WHILE '(' cond ')' stmt
    | BREAK ';' | CONTINUE ';' | RETURN exp? ';';
```

其中 `if-else` 的 dangling-else 歧义由 ANTLR 的默认"就近匹配"策略解决——`ELSE` 总是与最近的 `IF` 配对。

== AST 设计

我们的 AST 采用 C++ 的强类型 `std::variant` 体系设计

=== 类型体系

基础类型定义在 `op.hpp` 中：

```cpp
enum class Type : uint8_t { INT, FLOAT, BOOL, DOUBLE, VOID };
enum class UnaryOp : uint8_t { PLUS, MINUS, NOT };
enum class BinaryOp : uint8_t {
    MUL, DIV, MOD, ADD, SUB,
    LT, GT, LEQ, GEQ, EQ, NEQ,
    AND, OR, INDEX
};
```

我们的 `BinaryOp::INDEX` 是一个设计亮点：数组下标访问 `a[i]` 被统一建模为二元运算 `INDEX(a, i)`，而非单独的节点类型。这使得所有二元运算可以共用同一套类型检查和代码生成逻辑。

=== 节点体系

AST 的核心节点通过 `std::variant` 组成 sum type，关键的类型别名如下：

```cpp
using Decl = std::variant<ConstDecl, VarDecl>;
using Exp  = std::variant<PrimaryExp, UnaryExp, BinaryExp, CallExp>;
using Stmt = std::variant<IfStmt, WhileStmt, ReturnStmt, BreakStmt,
                          ContinueStmt, AssignStmt, BlockStmt, ExpStmt>;
```

由于 `Exp` 和 `Stmt` 涉及递归引用（如 `BinaryExp` 包含两个子表达式，`IfStmt` 包含子语句），我们引入了 `ExpBox` 和 `StmtBox` 作为间接层，内部持有 `std::unique_ptr`：

```cpp
struct ExpBox {
    std::unique_ptr<Exp> exp;
    // ...
};
struct StmtBox {
    std::unique_ptr<Stmt> stmt;
    // ...
};
```

这样既解决了 `std::variant` 不能直接包含自身（递归类型）的问题，又通过 `unique_ptr` 实现了堆上分配，保证了内存效率。

同时我们也让 sum type 中的每一项类型都继承了 `mixin::ToBoxed`，让它们能方便的转换为 Boxed Type.

```cpp
template <typename Self, typename Target = Self> struct ToBoxed {
private:
    ToBoxed() = default;

public:
    std::unique_ptr<Target> toBoxed() && { // 用右值，把原来对象的所有权交给新生成的 unique_ptr
        return std::make_unique<Target>(std::move(*static_cast<Self*>(this)));
    }
    friend Self;
};
```

由于 `std::variant` 提供的 `std::get`，`std::holds_alternative` 比较难用，我们给 `std::visit` 简单封装了一下实现了对于 variant 的方便访问

```cpp
template <typename... Ts> struct Visitor : Ts... {
    using Ts::operator()...;
};

template <typename... Ts> Visitor(Ts...) -> Visitor<Ts...>;

template <typename T, typename... Fs> auto match(T&& expr, Fs&&... callbacks) {
    return std::visit(Visitor{std::forward<Fs>(callbacks)...}, std::forward<T>(expr));
}

template <typename... Ts> struct Match {
    std::tuple<Ts...> values;
    Match(Ts&&... val) : values(std::forward<Ts>(val)...) {}
    template <typename... Fs> auto operator()(Fs&&... callbacks) {
        return std::apply(
            [&](auto&&... vs) {
                return std::visit(Visitor{std::forward<Fs>(callbacks)...},
                                  std::forward<decltype(vs)>(vs)...);
            },
            values);
    }
    template <typename T> Match<Ts..., T> with(T&& val) && {
        return Match<Ts..., T>(std::forward<Ts>(values)..., std::forward<T>(val));
    }
    template <typename... T2s> Match<Ts..., T2s...> with(Match<T2s...>&& other) && {
        return std::apply(
            [](auto&&... args) {
                return Match<Ts..., T2s...>(std::forward<decltype(args)>(args)...);
            },
            std::tuple_cat(std::move(values), std::move(other.values))
        );
    }
};

template <typename... Ts> Match(Ts&&...) -> Match<Ts&&...>;
```

1. 原理：
- `Visitor` 模板与 Overloaded 模式：利用 C++17 的参数包展开与多重继承，将传入的一组 lambda 闭包聚合为一个具有重载 `operator()` 的类。配合 C++17 的自定义推导指引（deduction guide）`template <typename... Ts> Visitor(Ts...) -> Visitor<Ts...>;`，使得我们可以直接用大括号传递 lambda 列表，编译器会自动生成包含所有分支的访问器。
- `match` 函数：对 `std::visit` 的无感封装，它把变体数据 `expr` 和所有的处理闭包 `callbacks` 桥接在一起，免去了每次显式构造 Visitor 的冗长代码。
- 多重匹配 `Match` 类：由于 `std::visit` 原生支持同时接受多个 `std::variant`（进行多维度的类型分发），而我们的 `match` 函数由于将可变参数视为某个匹配项，这里我们构建了一个 `Match` 类，先传入可变数量的variant构建，然后调用时再传入可变数量的lambda，同时也能通过 `with()` 合并多个 `Match` 类。最后调用 `operator()` 时，借助 `std::apply` 把 tuple 解包展开为参数列表，交由 `std::visit` 统一分发。

*2. 使用效果*：
这套封装大幅简化了 AST 的遍历与处理，将原先繁琐的 `std::holds_alternative` 判断和 `std::get` 转换，简化为类似其他语言中强大的模式匹配语法。例如，在语义分析中处理表达式时：

```cpp
match(exp,
    [&](const BinaryExp& bin) {
        // 直接处理二元表达式节点，bin 的类型已安全推导
    },
    [&](const UnaryExp& unary) {
        // 直接处理一元表达式节点
    },
    [&](const auto& other) {
        // 利用 auto 实现 fallback 通配符分支
    }
);
```

多个variant也能一起处理：

```cpp
Match{left_type, right_type}(
    [&](const Int& l, const Int& r) { /* 整数运算优化 */ },
    [&](const Float& l, const Float& r) { /* 浮点运算逻辑 */ },
    [&](const auto& l, const auto& r) { /* fallback 或类型不匹配报错 */ }
);
```

这种设计极大提升了 `std::variant` 在编译器开发中的工程体验，代码精简且类型安全。

=== 主要节点结构

所有节点都继承 `mixin::Locatable` 以携带源码位置信息，下面列出关键节点的设计：

*1. 表达式节点：*

- `PrimaryExp`：基本表达式，可以是括号子表达式（`ExpBox`）、左值（`LValExp`）或常量（`ConstExp`）。
- `UnaryExp`：一元运算，包含运算符 `UnaryOp` 和操作数 `ExpBox`。
- `BinaryExp`：二元运算，包含运算符 `BinaryOp`、左操作数 `ExpBox` 和右操作数 `ExpBox`。
- `CallExp`：函数调用，包含函数名 `LVal` 和参数列表 `FuncArgs`。

*2. 左值设计：*

`LVal` 表示简单的变量名称。`LValExp` 使用 `std::variant<LVal, BinaryExp>` 建模——简单左值直接为 `LVal`，多维数组访问 `a[i][j]` 则展开为嵌套的 `BinaryExp(INDEX)`：

```
a[i][j]  →  INDEX(INDEX(a, i), j)
```

*3. 语句节点：*

- `AssignStmt`：包含左值 `LValExp` 和右值 `Exp`。
- `IfStmt`：cond 为 `Exp`，then 分支为 `StmtBox`，else 分支为 `optional<StmtBox>`。
- `WhileStmt`：cond 为 `Exp`，循环体为 `StmtBox`。
- `BlockStmt`：块语句，`items` 为 `vector<variant<Decl, Stmt>>`，块内既可包含声明也可包含语句。

*4. 顶层结构：*

```cpp
struct CompUnit {
    using Item = std::variant<Decl, FuncDef>;
    std::vector<Item> items;
};
```

`CompUnit` 被标记为不可移动（`CompUnit(CompUnit&&) = delete`），因为语义分析阶段使用 AST 节点地址作为 map 的键，移动操作会使这些地址失效。

== 生成 AST

AST 的生成通过 `ASTVisitor`（继承自 ANTLR 生成的 `CACTBaseVisitor`）实现，核心流程如下：

=== 整体流程

入口函数 `ast::parse` 封装了从输入流到 AST 的完整流水线：

```cpp
auto parse(std::istream& stream) -> std::unique_ptr<ast::CompUnit> {
    ANTLRInputStream input(stream);
    CACTLexer lexer(&input);
    CommonTokenStream tokens(&lexer);
    CACTParser parser(&tokens);
    // 注册自定义错误监听器
    CACTErrorListener listener;
    lexer.removeErrorListeners();
    lexer.addErrorListener(&listener);
    parser.removeErrorListeners();
    parser.addErrorListener(&listener);
    // 使用 Visitor 遍历解析树，构建 AST
    ASTVisitor visitor;
    return unique_ptr<CompUnit>(
        any_cast<CompUnit*>(visitor.visit(parser.compUnit())));
}
```

=== 类型擦除与传递机制

由于 ANTLR 的 Visitor 接口要求所有 `visit` 方法返回 `std::any`，我们设计了一套基于 `shared_ptr` 的包装-解包机制：

```cpp
template <typename T> static std::any wrap(T&& val) {
    return make_any<node_ptr<T>>(make_shared<T>(forward<T>(val)));
}
template <typename T> static T take(std::any a) {
    return std::move(*any_cast<node_ptr<T>>(a));
}
```

`wrap` 将值包装为 `shared_ptr` 后存入 `std::any`，`take` 从中提取并移动出值。这样既满足了 `std::any` 的值语义要求，又避免了不必要的对象拷贝。

=== 关键 visit 方法

*1. 编译单元*。`visitCompUnit` 遍历所有子节点，按类型分别收集为 `Decl` 或 `FuncDef`，汇总到 `CompUnit::items` 中。

*2. 声明*。`visitConstDef` / `visitVarDef` 从 parse tree 中提取变量名、数组维度和初始化值。数组维度从 `INT_LITERAL` 节点列表中依次解析，支持 `std::stoi` 的自动进制推导（十进制、八进制、十六进制）。

*3. 表达式*。各层级的 `visitXxxExp` 方法实现了从 parse tree 到 AST 的优先级保持转换。以 `visitAddExp` 为例：

```cpp
std::any visitAddExp(CACTParser::AddExpContext* ctx) override {
    if (ctx->addExp()) {  // 存在递归：addExp ('+' | '-') mulExp
        BinaryExp bin{
            .op = (ctx->children[1]->getText() == "+")
                      ? BinaryOp::ADD : BinaryOp::SUB,
            .left  = make_unique<Exp>(take<Exp>(visit(ctx->addExp()))),
            .right = make_unique<Exp>(take<Exp>(visit(ctx->mulExp())))};
        bin.loc = get_loc(ctx->children[1]);
        return wrap(Exp(std::move(bin)));
    }
    return visit(ctx->mulExp());  // 直接透传到更高优先级
}
```

若当前规则仅为单个子表达式（无递归），则直接向下透传；若存在二元运算，则构造 `BinaryExp` 节点。

*4. 左值与数组下标*。`visitLVal` 将多维下标 `a[e1][e2]` 展开为嵌套的 `INDEX` 二元运算。第一个下标以变量名 `LVal` 为左操作数构造第一层 `BinaryExp(INDEX)`，后续下标以前一层的 `BinaryExp` 为左操作数继续嵌套。

*5. 函数调用*。`visitUnaryExp` 中检测到 `ID '(' funcArgs? ')'` 模式时，构造 `CallExp` 节点，将函数名存入 `LVal`，参数列表通过 `visitFuncArgs` 递归解析。

=== 位置信息传播

每个 visit 方法都通过 `get_loc(ctx)` 提取 parse tree 节点的行号和列号，赋值给 AST 节点的 `loc` 字段。`get_loc` 支持 `ParserRuleContext`、`TerminalNode` 等多种节点类型的统一位置提取，确保后续的错误报告能精确定位到源码位置。

= 思考题

#theorion.problem[
  如何把表达式优先级体现在文法设计中？
]

#theorion.solution[
  在上下文无关文法中，表达式优先级通过*层级化的产生式规则*来体现。核心思想是：优先级越低的运算符放在越上层的规则中，优先级越高的运算符放在越下层的规则中。每一层规则的右部引用下一层规则，从而确保解析树的结构天然反映出优先级层次。

  以 `CACT.g4` 为例，表达式规则从上到下依次为：

  ```antlr
  lOrExp  : lAndExp  | lOrExp '||' lAndExp;    // 第6层（最低）
  lAndExp : eqExp    | lAndExp '&&' eqExp;      // 第5层
  eqExp   : relExp   | eqExp ('==' | '!=') relExp; // 第4层
  relExp  : addExp   | relExp ('<'|'>'|...) addExp; // 第3层
  addExp  : mulExp   | addExp ('+' | '-') mulExp;   // 第2层
  mulExp  : unaryExp | mulExp ('*'|'/'|'%') unaryExp;// 第1层（最高二元）
  unaryExp: ('+' | '-' | '!') unaryExp | primaryExp | ID '(' funcArgs? ')';
  ```

  当解析 `a + b * c` 时，解析器首先进入 `addExp`，发现 `+` 运算符后将 `a` 和 `b * c` 分别作为左、右操作数。由于 `b * c` 匹配 `mulExp` 规则（`addExp` 的下一层），`*` 运算在解析树中位于更深层，从而自然获得了更高的优先级。

  此外，使用*左递归*（如 `addExp: addExp '+' mulExp`）自然实现了*左结合性*——在 `a + b + c` 中，`a + b` 先被归约为 `addExp`，再与 `c` 组合，形成 `(a + b) + c` 的结构。
]

#theorion.problem[
  如何设计数值常量的词法规则？
]

#theorion.solution[
  数值常量的词法规则需要覆盖整数和浮点数的各种合法表示形式，同时要利用规则优先级避免歧义。

  *整数字面量*支持三种进制：
  - *十进制*：以非零数字 `[1-9]` 开头，后跟任意多个数字 `DIGIT*`；
  - *八进制*：以 `0` 开头，后跟零个或多个八进制数字 `DIGIT_OCT*`（其中单独的 `0` 也是合法的八进制表示）；
  - *十六进制*：以 `0x` 或 `0X` 开头，后跟一个或多个十六进制数字 `DIGIT_HEX+`。

  ```antlr
  fragment DIGIT: [0-9];
  fragment DIGIT_OCT: [0-7];
  fragment DIGIT_HEX: [0-9a-fA-F];
  INT_LITERAL:
      [1-9] DIGIT* | '0' DIGIT_OCT* | ('0x' | '0X') DIGIT_HEX+;
  ```

  *浮点字面量*通过 `fragment FLOAT_NUMBER` 统一定义底层格式，涵盖了三种有效形式：
  - 整数部分`.`小数部分（可选指数）：如 `3.14`、`1.0e5`；
  - 仅小数部分（`.`开头）：如 `.5`、`.1e-3`；
  - 仅整数部分带指数：如 `1e10`。

  然后通过后缀区分类型：

  ```antlr
  FLOAT_LITERAL: FLOAT_NUMBER [fF];   // 带 f/F 后缀 → float
  DOUBLE_LITERAL: FLOAT_NUMBER;        // 无后缀 → double
  ```

  使用 `fragment` 关键字定义的 `DIGIT`、`DIGIT_OCT`、`DIGIT_HEX` 和 `FLOAT_NUMBER` 不会作为独立 Token 被词法分析器输出，它们仅作为其他规则的构建块，方便的同时避免了不必要的 Token 类型引入。

  布尔常量单独作为关键字处理：
  ```antlr
  TRUE: 'true';
  FALSE: 'false';
  ```
]

#theorion.problem[
  如何实现异常处理以做到在发现语法错误时及时报错？
]

#theorion.solution[
  我们通过自定义 ANTLR 的错误监听器来实现异常处理。具体做法是：

  1. 自定义 `CACTErrorListener`，继承 `antlr4::BaseErrorListener`，重写 `syntaxError` 方法：

    ```cpp
    class CACTErrorListener : public antlr4::BaseErrorListener {
    public:
        void syntaxError(Recognizer* recognizer, Token* offendingSymbol,
                        size_t line, size_t charPositionInLine,
                        const std::string& msg, std::exception_ptr e) override {
            if (dynamic_cast<antlr4::Lexer*>(recognizer)) {
                throw LexicalError(Location{(int)line, (int)charPositionInLine}, msg);
            } else {
                throw SyntacticError(Location{(int)line, (int)charPositionInLine}, msg);
            }
        }
    };
    ```

    该方法通过 `dynamic_cast` 判断错误发生在词法阶段还是语法阶段，分别抛出 `LexicalError` 或 `SyntacticError` 异常。异常携带了精确的行号和列号信息。

  2. 替换默认错误监听器。在解析流程中，先移除 ANTLR 默认的 `ConsoleErrorListener`，再注册我们的自定义监听器：

    ```cpp
    CACTErrorListener listener;
    lexer.removeErrorListeners();
    lexer.addErrorListener(&listener);
    parser.removeErrorListeners();
    parser.addErrorListener(&listener);
    ```

    这样做的原因是，ANTLR 默认的错误处理策略会尝试恢复并"继续"解析（例如插入/删除 Token），同时仅向 stderr 输出错误信息。在编译器场景中，我们期望在遇到第一个错误时立刻停止，并通过异常机制将错误信息传播到上层。

  3. 异常驱动的错误传播。抛出的 `LexicalError` / `SyntacticError` 是项目中统一定义的编译错误类型。上层的 `main` 函数或测试框架捕获这些异常后，可以输出错误信息并返回非零退出码，满足实验的"编译器对存在错误的样例应返回任意非零值"的要求。

    异常继承链：

    ```cpp 
    struct CodeError : std::runtime_error;

    struct SyntaxError : CodeError;
    struct LexicalError : SyntaxError;
    struct SyntacticError : SyntaxError;

    struct SemanticError : CodeError;

    struct CompilerError : std::logic_error;
    ```

    主函数中的处理：
    ```cpp 
    try {
        // ...
    } catch (const SyntaxError& e) {
        // ...
    } catch (const SemanticError& e) {
        // ...
    }
    ```

    这种"抛异常即终止"的策略简单直接，在发现第一个词法或语法错误后立即报告并中止编译流程。
]
