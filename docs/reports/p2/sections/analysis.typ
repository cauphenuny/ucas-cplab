#import "../../preamble/preamble.typ": *

== 用到的数据结构

因为感觉把所有东西都写到grammar里面很臃肿，还跟文法耦合了，所以在分析之前，我们直接重建了整个 AST，而不是在 antlr 的节点里面指向 AST 节点，并且 AST 节点只包含 AST 子节点，具体的信息由其他数据结构维护 (比如 `SemanticAST` 结构体维护语义信息)

实现上来说，因为其他数据结构中通过 AST 节点指针来作为 key，所以需要删掉作为 key 的 AST 节点类型的 move 构造函数，IR 节点同理。

我们的 AST 数据结构定义如下：

函数
```cpp
struct FuncDef { Type type; std::string name; FuncParams params; BlockStmt block; };
struct FuncParam { Type type; std::string name; std::vector<std::optional<size_t>>> dims; };
using FuncParams = std::vector<FuncParam>;
using FuncArgs = std::vector<Exp>;
```

定义
```cpp
struct ConstDecl { Type type; std::vector<ConstDef> defs; };
struct VarDecl { Type type; std::vector<VarDef> defs; };
using Decl = std::variant<ConstDecl, VarDecl>;
```

语句
```cpp
struct IfStmt { Exp cond; StmtBox stmt; };
struct WhileStmt { Exp cond; StmtBox stmt; std::optional<StmtBox> else_stmt; };
struct ReturnStmt { std::optional<Exp> exp; };
struct BreakStmt {};
struct ContinueStmt {};
struct AssignStmt { LValExp var; Exp exp; };
struct BlockStmt { std::vector<std::variant<Decl, Stmt>> items; };
struct ExpStmt { Exp exp; };
using Stmt = std::variant<IfStmt, WhileStmt, ReturnStmt, BreakStmt, ContinueStmt, AssignStmt, BlockStmt, ExpStmt>;
struct StmtBox;
```

表达式
```cpp
struct LValExp { std::string name; };
struct PrimaryExp { std::variant<ExpBox, LValExp, ConstExp> exp; };
struct UnaryExp { UnaryOp op; ExpBox exp; };
struct BinaryExp { BinaryOp op; ExpBox left, right; };
struct CallExp { LVal func; FuncArgs args; };
using Exp = std::variant<PrimaryExp, UnaryExp, BinaryExp, CallExp>;
struct ExpBox { std::unique_ptr<Exp> exp; };
```

---

用于当 key 的节点类型:

```cpp
using SymDefNode = std::variant<const ConstDef*, const VarDef*, const FuncParam*, const FuncDef*>;
using VarDefNode = std::variant<const ConstDef*, const VarDef*, const FuncParam*>;
using FuncDefNode = const FuncDef*;
using LValNode = const LVal*;
using ExprNode = std::variant<const ConstDef*, const VarDef*, const FuncDef*, const LValExp*,
                              const Exp*, const PrimaryExp*, const UnaryExp*, const BinaryExp*,
                              const ExpBox*, const ConstExp*, const CallExp*, const FuncParams*,
                              const FuncArgs*, const FuncParam*, const ConstInitVal*, const LVal*>;

using StmtNode =
    std::variant<const StmtBox*, const Stmt*, const IfStmt*, const WhileStmt*, const ReturnStmt*,
                 const AssignStmt*, const BreakStmt*, const ContinueStmt*, const BlockStmt*,
                 const ExpStmt*, const Decl*, const ConstDecl*, const VarDecl*>;
```

---

Semantic AST 维护的信息：

- 函数表
- 每一个变量使用处对应的定义
- 不可变变量集合
- 每一个表达式对应的类型
- 每一个语句的类型（返回值类型、是否一定返回）

```cpp
struct SemanticAST {
    using VarTable = std::unordered_map<std::string, VarDefNode>;
    using FuncTable = std::unordered_map<std::string, std::pair<FuncDefNode, bool>>;
    FuncTable funcs;
    std::vector<VarTable> vars;
    std::unordered_map<LValNode, SymDefNode> defs;
    std::unordered_set<VarDefNode> readonly_defs;  // variables that are declared as const
    std::unordered_map<ExprNode, Type> types;
    struct StmtType {
        Type ret_type{NEVER};
        bool always_return{false};
    };
    std::unordered_map<StmtNode, StmtType> stmt_types;
}
```

---

Semantic AST 构造时自上至下遍历 AST，计算上述信息

定义
```cpp
    void analysis(const CompUnit* comp_unit);
    void analysis(const Decl* decl);
    void analysis(const ConstDecl* decl);
    void analysis(const VarDecl* decl);
    void analysis(const ConstInitVal* val);
```
函数
```cpp
    void analysis(const FuncParam* param);
    void analysis(const FuncParams* params);
    void analysis(const FuncArgs* args, const ir::type::Product& param_types);
    void analysis(const FuncDef* func_def, bool is_builtin = false);

```
语句
```cpp
    void analysis(const Stmt* stmt);
    void analysis(const BlockStmt* block);
    void analysis(const IfStmt* if_stmt);
    void analysis(const WhileStmt* while_stmt);
    void analysis(const ReturnStmt* return_stmt);
    void analysis(const AssignStmt* assign_stmt);
    void analysis(const ExpStmt* exp_stmt);
    void analysis(const StmtBox* stmt_box);
```
表达式
```cpp
    void analysis(const Exp* exp, const Type& upperbound = ANY, bool readonly = true);
    void analysis(const LVal* lid, const Type& upperbound = ANY, bool readonly = true);
    void analysis(const LValExp* lval, const Type& upperbound = ANY, bool readonly = true);
    void analysis(const CallExp* call, const Type& upperbound = ANY, bool readonly = true);
    void analysis(const ConstExp* const_exp, const Type& upperbound = ANY, bool readonly = true);
    void analysis(const PrimaryExp* primary, const Type& upperbound = ANY, bool readonly = true);
    void analysis(const UnaryExp* unary_exp, const Type& upperbound = ANY, bool readonly = true);
    void analysis(const BinaryExp* binary_exp, const Type& upperbound = ANY, bool readonly = true);
    void analysis(const ExpBox* exp_box, const Type& upperbound = ANY, bool readonly = true);
```

== 变量/函数定义检查

我们在遍历 AST 的同时维护一个函数表和一个变量定义表：

1. 函数注册：
    - 在语义分析开始时先注册内建函数，再分析用户代码。
    - `registerFunction` 会检查函数名是否已存在，若存在则报“函数重定义”。
    - 由于函数表是全局的，因此不存在同名函数重载，内建函数名也不可被覆盖。

2. 变量注册：
    - `registerVariable` 只检查当前作用域（`vars.back()`）。
    - 同一作用域内重复定义会报错；不同作用域允许同名变量遮蔽。

3. 作用域维护：
    - 进入函数体时 `pushScope()`，先注册形参，再分析函数体，最后 `popScope()`。
    - 进入 `BlockStmt` 时再 `pushScope()`，块结束时 `popScope()`。
    - `lookup` 从内向外查找。

4. 入口函数检查：
    - 分析完 `CompUnit` 后检查 `main` 是否存在。
    - 同时要求 `main` 的类型为 `int()`，否则报错。

== 类型检查

=== 表达式类型检查

我们在遍历 AST 的同时计算每一个表达式的类型。

- 在每一个表达式位置，有一个继承属性 `upperbound` 表示当前表达式类型必须是 `upperbound` 的子类型，比如 `if (a) ...` 中 `a` 的 `upperbound` 就是 `bool`， `array[e]` 中 `e` 的 `upperbound` 就是 `int`，函数调用中实参的 `upperbound` 就是形参的类型，这样做能统一处理各种位置的类型要求。
  
  如果计算出的类型不是 `upperbound` 的子类型，报出类型错误。
  
  同时，在一些特殊表达式的类型计算中也会抛出类型错误，具体地：
  
  - 函数调用参数个数不符
  - 二元算数逻辑表达式中，左右子表达式类型不兼容

  upperbound 属性也能用于分析LVal应该在函数符号表查找还是变量符号表查找，因为函数调用表达式处的LVal的 `upperbound` 会被设置为 `Func{.param = ANY, .ret = NEVER}`，只需要判断LVal处的 `upperbound` 是否是函数类型即可。

- 表达式还有一个继承属性 `readonly`，可以理解为对于此处表达式的要求：如果 `readonly = false`，要求此处表达式可变，目前只用于 LVal 表达式，不能修改常量，否则报出类型错误。

- 对于数组类型，我们做了一些特殊处理：为了便于 IR 生成与执行，我们在计算左值表达式的类型时自动将数组退化成引用，根据数组定义是否为常量退化为不可变引用或可变引用。多维引用取元素时，也自动退化成引用。

---

=== 语句类型检查

语句类型的定义是：执行完它可能返回什么类型，以及是否一定会返回。

我们在 StmtType 结构体中实现一些辅助函数：

- append: 将另一个 StmtType 追加到当前 StmtType 上，表示在当前语句后面执行另一个语句，返回值类型是两者的并集，是否一定返回由追加的 stmt 决定。
- merge: 将另一个 StmtType 合并到当前 StmtType 上，表示合并两种执行路径，返回值类型是两者的并集，如果其中一个语句不一定返回，那么合并后的 StmtType 也不一定返回。

```cpp
struct StmtType {
    Type ret_type{NEVER};
    bool always_return{false};
    void append(const StmtType& next) {
        if (always_return) return;  // if already always return, no need to append
        ret_type = ret_type | next.ret_type;
        always_return = next.always_return;
    }
    void merge(const StmtType& next) {
        ret_type = ret_type | next.ret_type;
        always_return = always_return && next.always_return;
    }
    TO_STRING(StmtType, ret_type, always_return);
};
```

Semantic AST 构造时，我们在遍历 AST 的同时计算每一个语句的 StmtType：

- 对于 if 语句，类型是 true 分支，如果存在 false 分支，合并两个分支的类型
- 对于 while 语句，类型是循环体的类型
- 对于 return 语句，类型是返回值的类型，标记 `always_return = true`
- 对于 block 语句，不断append每一个语句的类型

计算出的类型用于函数的类型检查，非 void 函数必须保证 `always_return = true`，否则报出类型错误。
同时检查函数实际返回的类型是否是函数声明的返回值类型的子类型，否则报出类型错误。

