# PR2-CACT-实验指导

注：本指导针对该实验中的一些重难点提供可行的处理方法，所展示的代码主要用于展示思路，同学们学习其中的思想即可，不必按照它们来做自己的实现。

## 语法分析树属性设计

### 遍历语法分析树

本实验需要以深度优先的顺序遍历语法分析树，以进行继承属性和综合属性的计算。

在 ANTLR 中，语法分析树的结点都是 `ParseTree`（源码详见“antlr4-runtime/tree/ParseTree.h”）的子类，继承关系为：

非终结符：`ParseTree`，`RuleContext` ，`ParserRuleContext`。

终结符：`ParseTree`，`TerminalNode` 。

它们的定义都可以在“antlr4-runtime”目录中的源文件中找到，且 `ParserRuleContext` 会派生出由用户在文法文件中定义的各非终结符相关的子类来表示语法分析树上的内部结点，这部分在“grammar/CACTParser.h” 中可以找到。例如一个 `comp_unit` 的非终结符会有如下定义：

```cpp
  class  Comp_unitContext : public antlr4::ParserRuleContext {
  public:
    ...
    Comp_unitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EOF();
    ...
    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };
```

其中“...”省略的部分表示可以添加的成员（结点属性）或类方法（与子结点相关），但它们的添加**需要在文法文件中进行**（稍后讨论）而不能由用户直接修改此处的代码。

学习这些源码（“CACTParser.h”“ParserRuleContext.h”等），可以找到结点的属性和方法。

在我们使用的 Visitor 模式下，当遍历到某个结点时，将导致相应的类方法被调用。例如遍历到 `const_decl` 结点时，将调用下面的方法（其中，`Analysis` 继承于 `CACTVisitor`，这类似于起始框架中的 Hello 的 demo 中 `Analysis` 继承于 `HelloVisitor`）：

```cpp
std::any Analysis::visitConst_decl(CACTParser::Const_declContext *ctx)
{
    ...
    visitChildren( ctx );
    ...
    return nullptr;
}
```

`visitChildren(ctx)` 将导致该结点的子节点被遍历，并且它们对应的 `visit***`方法也随之被调用。所以一般而言，对结点属性的计算就在其前后，即上述代码片段中省略的“...”位置。

此外，某些情况下可能需要手动逐个遍历子结点，这时请使用 `accept` 方法，例如手动访问该结点的 `expr` 子结点可使用：

```cpp
ctx->expr()->accept(this);
```

这也会导致其 `expr` 这个子结点被访问并且相应的 `visitExpr()` 方法被调用。实际上，`visitChildren()` 的实现原理就是对子结点逐个 `accept(this)`。这部分代码参见“deps/antlr4-runtime/tree/AbstractParseTreeVisitor.h”。

### 为结点添加属性

前面已经反复提到不要直接修改“grammar”目录下生成的 C++ 源文件，因为它们是 ANTLR 自动生成的，会随文法文件的修改而被轻易覆盖。当需要给语法分析树上的结点（上述提到的 `Comp_unitContext` 等类）添加属性时，应直接在文法文件中定义。

ANTLR 提供了向结点添加属性的办法，即 `locals` 指令，例如往 `comp_unit` 结点增加 `scope` 属性，其（在 C++ 程序中的）类型为 `HSymbolTable*`，则在文法文件“CACT.g4”中有：

```c
comp_unit
    locals [
        HSymbolTable* scope,
    ]
    : (decl|func_def)* EOF
        ;
```

需要注意的是，当添加多个属性时，请用逗号 `,` 而非 C++ 的分号 `;` 隔开。

这将导致“CACTParser.h”中对 `Comp_unitContext` 的定义变成了下面这样：

```cpp
  class  Comp_unitContext : public antlr4::ParserRuleContext {
  public:
    HSymbolTable* scope;
    Comp_unitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EOF();
    std::vector<DeclContext *> decl();
    DeclContext* decl(size_t i);
    std::vector<Func_defContext *> func_def();
    Func_defContext* func_def(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };
```

可以看到其中出现了与用户在“CACT.g4”中的定义相对应的 `scope`、`decl()`、`func_def()` 等类成员和方法。由于（根据“CACT.g4”中的描述） `decl`、`func_def` 子结点可能出现多次，所以这里用了 `std::vector<>` 的数据结构来表示，当然也可以用 `decl(size_t i)`、`func_def(size_t i)` 方法来索引其中的第 `i` 个。

然而，用户添加的属性可能依赖于某些定义（例如 `HSymbolTable` 类），这时可以通过在文法文件中添加 `@header` 指令来让 ANTLR 自动将头文件包含添加到生成的 C++ 源文件中。例如，在“CACT.g4”开头仅次于 `grammar CACT;` 的位置添加：

```c
@header {
	#include "HExpression.h"
	#include "HFunctionTable.h"
}
```

这将导致生成的“CACTParser.h”开头出现下面的包含：

```cpp
#include "HExpression.h"
#include "HFunctionTable.h"
```

当然，修改文法文件后，记得重新用 Java 命令调用 ANTLR 生成词法语法分析器。

### 属性的传递

在语法分析树的各结点之间传递和计算属性是语义分析部分的核心任务。下面分几种情况简单讨论一下相应的处理思路。

#### 属性的类型

**综合属性**

计算一个结点的某个综合属性需要在 `visitChildren()` 之后进行。这很好理解，因为综合属性取决于结点本身及其子结点的属性（定义）。

**继承属性**

对于较简单的情形——仅依赖于父结点的属性，可以考虑两种方式，pull 和 push。

pull 是指由该子结点通过其 `parent` 指针访问父结点，从而获取其属性。这并不推荐，因为 `parent` 是指向基类 `ParseTree` 的指针（定义在“deps/antlr4-runtime/tree/ParseTree.h”），需要做 `dynamic_cast<>`，容易出错；此外，某个非终结符可能出现在多个规则的右端，找其父节点还需要引入更多判断的逻辑，增加了复杂性。

push 则是指在子结点中创建属性，由父结点遍历其各个子结点并进行赋值，这比较简单直接，是推荐的做法。例如，假如有一条规则描述一个语句块可以派生出多条语句，且这些语句继承该语句块的作用域：

```c
block:
    '{' stmt* '}'
    ;
```

那么就可以在访问到 `block` 时，用下面的程序将其作用域属性赋值给各表示语句的子结点：

```cpp
for (auto s : ctx->stmt())
	s->scope = ctx->inner_scope;
```

对于较复杂的情形——需要其父结点中排在更前面的子结点的属性的属性，也可以使用前文所述的 pull 方式，但出于同样的原因并不推荐。一个典型的例子是变量的定义与初始化，这将在后面“复杂情形的处理”部分详细讨论。

#### 属性的传递方向

有些语义分析功能可以从两个方向来实现，例如检查变量初始化类型。设有下面的描述变量定义与初始化的语法规则：

```c
var_decl:
    b_type var_def (',' var_def)* ';'
    ;

var_def:
    ID ('[' Int_const ']')* ('=' const_init_val)?
    ;
```

其中 `ID` 代表标识符（变量名），其类型由 `btype` 决定；`const_init_val` 代表可选的初始化常量。由于 CACT 不允许类型转换，所以需要检查它们的类型是否匹配。例如下面的初始化：

```c
    int k1806 = 75038;
```

为了检查初始化常量 `75038` 是否匹配变量 `k1806` 的 `int` 类型，可以自顶向下也可以自底向上。前者是指，把变量类型 `b_type`（这里的 `int`）传递给变量定义 `var_def` 或初始值 `const_init_val` 然后在访问它时进行比较。后者是指，先访问 `const_init_val` 确定该常量的属性，再到 `var_def` 层面将其与定义的类型进行比较。通常认为后者更简便，因为它是一种更易操控的综合属性。

## 符号表与函数表

语义分析阶段有两个重要的数据结构：符号表和函数表。它们分别用来记录各个作用域下的符号（变量），和整个程序全局的所有函数。

### 符号表

符号表的基本原理可参考 PPT（截图如下），每次进入新的作用域时创建一个符号表保存在其中定义的符号，需要访问符号时自底向上查找即可。当然，大家也可以把符号表设置成树状，离开作用域时并不删除该作用域的符号表，而是将一个指向当前作用域的指针回退到其父结点，再遇到新的作用域时创新新的符号表作为其新的子结点。这样可能带来的好处是能够保留更多的信息，从而便于调试。

<img src="image/image-20251008172552084.png" alt="image-20251008172552084" style="zoom: 67%;" />

### 函数表

CACT 允许函数名和变量名相同，这就让函数与变量成为两个独立的部分，帮大家松耦合。

可以设计全局函数表（直接用一个 `std::vector<>` 亦可），记录每个函数的函数名、形式参数、返回值等信息。每当遇到函数定义时即在表中注册，遇到函数调用时检查其与表中信息是否相符。

需要注意的是，为了支持递归调用（函数调用自己），对函数的注册应当在访问其语法分析树上的子结点（`visitChildren()`）之前，这样才能确保在函数体内遇到对自身的调用时，该函数已经注册。

## 复杂情形的处理

### 变量定义

设有变量定义与初始化的语法规则（参见“属性的传递方向”部分），多个变量的定义（`var_def`）都需要语句开始声明的类型（`b_type`）。例如下面的定义：

```c
   int k = 120, t = 140, z = 160;
```

`b_type` 为 `int`，使得后面的三个 `var_def` 定义的变量（`k`，`t`，`z`）都是 `int` 类型。

处理这种情形一个推荐的思路如下：在 `b_type` 新建一个列表 `pass_to`，这个列表的每个元素是指针，这个指针指向一个表示基本类型的变量。在 `var_def` 设置一个变量 `base_type`，代表该变量被定义为的类型（用于初始化常量的类型检查以及符号表的填充等）。这样就有下面的规则：

```c
b_type
    locals [
        std::vector<Btype *> pass_to,
    ] :
    INT
    | BOOL
    | FLOAT
    | DOUBLE
    ;

/* ... */

var_def
    locals [
        cact_base_type_t base_type,
    ] : ID ('[' Int_const ']')* ('=' const_init_val)?
	;
```

当遍历到它们共同的祖先（`var_decl`）时，获取所有的 `var_def` 子结点的 `base_type` 变量，加入该列表 `pass_to`：

```cpp
//  enter 'var_decl'
    for (auto def : ctx->var_def())
        ctx->b_type()->pass_to.push_back(&def->base_type);
```

然后，当 `b_type` 访问完毕，其类型已判断完成后，将值写入列表中的指针：

```cpp
// exit 'b_type'
// Assume that the type of this 'b_type' node has been determined as `tp`
    for (auto *pt : ctx->pass_to)
    {
        assert(pt != nullptr);
        *pt = tp;
    }
```

这样写看似简单，实际上一点也不困难。

### 多维数组

#### 数据类型定义

CACT 不仅支持基础数据类型（`int`、`float` 等）还支持多维数组，因此对于其中的变量、常量等数据，**需要一个统一的数据结构以准确描述其类型**。

首先需描述其基础类型（`base_type`），这可以用一个枚举数据结构（定义 `UNKNOWN` 是为了方便调试未赋值的错误）：

```cpp
enum class Btype {UNKNOWN = 0, VOID, INT, BOOL, FLOAT, DOUBLE};
```

然后，需要描述其（作为数组的）维度（`dim`），一个简单的方法是用一个无符号整型数组（例如 `std::vector<uint32_t>`），分别描述各个维度的长度。这样，`dim.at(i)` 就是数组在第 `i` 个维度的长度，而 `dim.size()` 即直接表示这是几维数组。举几个例子：

变量 `int daysofmonths[12]` 是一维数组，其类型的 `dim` 为 `{12}`；

初始化常量 `{1, 2, 3, 0, 6}` 是一维数组，其类型的 `dim` 为 `{5}`；

变量 `double score[4][5][6]` 是三维数组，其类型的 `dim` 为 `{4, 5, 6}`；

初始化常量 `{ {5, 0}, {3, 3}, {0, 5} }` 是二维数组，其类型的 `dim` 为 `{3, 2}`；

特别的，变量 `int K1806` 是“标量”，其类型的 `dim` 为 `{ }`（空数组）。

可以发现它们的 `dim.at(i)` 和 `dim.size()` 均满足上述约定。

最后，还需要描述其是否为 `const` 变量（`is_const`），这直接使用一个 `bool` 类型的变量即可。

综上所述，我们可以使用下面的数据结构来描述 CACT 的所有数据类型：

```cpp
/*
 * Type for var def, expr, ...
 * For example, { {5, 0}, {3, 3}, {0, 5} } has type:
 * true, Btype::INT, {3, 2}.
 */
struct cact_type_t {
    bool is_const;
    Btype base_type;
    // `dim.size()` is the number of dimensions
    std::vector<uint32_t> dim;
};
```

#### 初始化检查

多维数组的初始化检查是一个难点。先重述一下 CACT 关于多维数组初始化的规则：支持且仅支持两种方式：

(1) 列出元素：无论几维，都可以用元素列表初始化，且数量可少，例如：

```c
int a[2][2][2] = {1,2,3,4,5,6};
// a[0][0][0] = 1 a[0][0][1] = 2
// a[0][1][0] = 3 a[0][1][1] = 4
// a[1][0][0] = 5 a[1][0][1] = 6
// a[1][1][0] = 0 a[1][1][1] = 0
```

(2) 嵌套定义：把子数组作为元素，这时内层初始化的元素数量仍然可少，但要求层数不能少：

```c
int a[2][4][2] = {
    {
        {1},
        { },
        {2,3}
    }
};
```

简单来说就是：要么完全展开，要么保留层数（维度）。所以，像下面这样混合的算错误：

```c
int a[4] = {1,2, {3,4}};          // Error
int b[2][2][2] = { {1,2,3,4} };   // Error
```

那么对于编译器而言，如何检查数组的初始化是否符合规范？假设有下面的语法规则来定义初始化值 `const_init_val`：

```c
const_init_val:
    const_expr
    | '{'(const_init_val (',' const_init_val)*)? '}'
    ;
```

首先需要确定 `const_init_val` 的类型。一种可行的思路是为每个 `const_init_val` 设置**综合属性** `type` 表示其数据类型。如果它直接产生了 `const_expr`（没有派生出带 `{ }` 的列表），那么其 `base_type` 可以直接确定，而 `dim` 也直接为空（标量）。如果它产生了新的列表（数组），那么它的 `base_type` 直接与子数组相同，而其维度 `dim` 可经过推导得到。下面做个简单的提示：

设一个 `const_init_val` 派生出了 $m$ 个子数组，它们的 `dim` 的长度（数组的层数）都是 $n$（如果不一样，就要报错了），这些 `dim` 分别是 $\{d_{11},\cdots,d_{1n}\},\{d_{21},\cdots,d_{2n}\},\cdots,\{d_{m1},\cdots,d_{mn}\}$，那么该 `const_init_val` 的 `dim` 应该是 $\{m,\max\{d_{11},d_{21},\cdots,d_{m1}\},\cdots,\max\{d_{1n},d_{2n},\cdots,d_{mn}\}\}$。

确定好其类型后，即可与变量定义的类型进行比较，设计算法判断该类型的初始化常量能否用于初始化该数组变量。

### 表达式

由于表达式要考虑优先级，所以一个简单的表达式的语法分析树可能会很长。例如，由语法规则中的一个 $Exp$ 推导出一个简单的加法表达式 `a + b` 可能需要经过多步派生：
$$
\begin{align}

Exp &\Rightarrow  AddExp \\
    & \Rightarrow AddExp\ + \ MulExp \\
    & \Rightarrow MulExp\ + \ MulExp \\
    & \Rightarrow UnaryExp\ + \ UnaryExp \\
    & \Rightarrow PrimaryExp\ + \ PrimaryExp \\
    & \Rightarrow LVal\ + \ LVal \\
    & \Rightarrow Ident\ + \ Ident \\
    & \Rightarrow\ \mathrm{a+b}

\end{align}
$$
所以其语法分析树也会很高。为了便于后续阶段的处理（表达式类型检查、中间代码生成等），通常需要为表达式构建一个**抽象语法树**（AST，参见理论课知识）。这样一个加法表达式的抽象语法树仅有三个结点：一个代表加法运算的根结点“`+`”及其所属的两个代表操作数的子结点“`a`”和“`b`”。

由语法分析树构建抽象语法树的方法可参考理论课的知识：

<img src="image/image-20251009104508727.png" alt="image-20251009104508727" style="zoom:67%;" />

<img src="image/image-20251009104552730.png" alt="image-20251009104552730" style="zoom:67%;" />

通过遍历语法分析树，生成相应的表达式抽象语法树的结点，组合成抽象语法树，并作为综合属性挂在语法分析树的结点上。这样，类型检查以及中间代码生成可通过遍历更简单的抽象语法树来实现。

### 函数的定义与调用

对于函数的定义与调用，有几处需要注意的地方。

#### 支持递归

前面已经提到过，为了支持递归调用，对函数的注册应当在访问其语法分析树上的子结点（`visitChildren()`）之前，这样才能确保在函数体内遇到对自身的调用时，该函数已经注册。

#### 数组作为参数

当函数的形参列表中有数组时，对函数的调用还要检查实参的数组的维度、长度是否匹配。与 C 语言类似，形参列表中的多维数组只有第一个维度可以省略。

```c
int foo(int x[2], int y[]) {
    return x[0] + y[0];
}

int main() {
    int a[2][2];
    foo(a[0], a[1]);
    return 0;
}
```

#### 返回语句

CACT 要求所有非 `void` 类型的函数都要有 `return` 语句，函数内语句的所有路径都要以 `return` 语句来结束，且返回的表达式的类型要与函数定义的类型相符。

判断路径是否都以 `return` 语句结束，可以根据函数体的最后一个语句的类型的情况，进行下面的判断：

(1) 如果是 `return` 语句，那么已经符合要求；

(2) 如果是 `while` 语句或没有 `else` 的 `if` 语句，那么直接认为是缺少 `return` 语句的语义错误，因为条件可能会不满足（这里不考虑常量传播，哪怕条件表达式是 `true` 也判为错）；

(3) 如果是 `if`-`else` 语句或一个新的复合语句（`block`），那么此时需要递归地判断它是否以 `return` 结束。

其他语句则不能作为最后一条语句。

## IR 的设计

IR 大家可以自行设计，但是小组在协作时要对此有一个明确的约定，最好是有一个统一的规范性文档，这样负责各部分的同学都按照此规范来进行。

一种简单的方式是参照理论课上学习的三地址码：

```assembly
t2 = t0 + t1
t4 = t0 * t3
a = t2 * t4
b = t2 - t4
```

也可以参考 LLVM IR 的格式来设计：

```assembly
define external i32 @main() {
    %0 = call i32 @get_int()
    %4 = icmp sle i32 %0, u0x0
    br i1 %4, label %2, label %3
2:
    ret i32 u0xffffffff
3:
    %5 = add i32 u0x1, u0x0
10:
    %11 = icmp sle i32 %5, %0
    br i1 %11, label %8, label %9
; ...
```

关于 LLVM IR，可参阅官方手册：https://llvm.org/docs/LangRef.html

当然，也可以直接选用 LLVM IR，但这要求对 LLVM IR 的各种特性（SSA 等）有深入的了解，可能会有比较大的难度：

```assembly
define dso_local range(i32 -1, 1) i32 @main() local_unnamed_addr #0 {
; ...
5:                                                ; preds = %0, %45
  %6 = phi i32 [ %48, %45 ], [ 0, %0 ]
  %7 = phi i32 [ %46, %45 ], [ 1, %0 ]
  %8 = icmp ugt i32 %7, 1
  br i1 %8, label %9, label %45
9:                                                ; preds = %5
  %10 = and i32 %6, 1
  %11 = icmp eq i32 %6, 1
```

IR 的设计需要注意以下几个方面：

(1) 为后续的分析和优化留下扩展空间，例如在函数层面建立基本块间的控制流图，从而可以为一系列基于数据流分析的优化所用。

(2) 具有良好的可调试性，可以完整地输出为人类可读的文本形式的表示，以供语义检查。在本实验阶段下，该部分主要用于检查 IR 是否与源程序语义等价。而在继续添加优化后，则需要保证优化前后程序的行为等价。

(3) 能够用简单的程序进行读取，以支持后续的解释执行。

## IR 的解释执行

设计模拟器用于 IR 的解释执行，对于编译器的开发调试具有重要作用。模拟器主要用于检查编译器生成的 IR 的正确性（格式、语义等），即使在后端开发调试时也可能派上用场（试想一个程序编译出的汇编代码是错误的，使用模拟器解释执行 IR 可以快速判断是前端错误还是后端错误）。所以，该模拟器应当具有下面的功能：

(1) 读取 IR 文本，检查其中是否有格式错误；

(2) 模拟一个程序运行的环境并维护；

(3) 在模拟环境中解释执行 IR，发现并报告其中的运行时错误（例如数组访问越界、运算溢出等）并报告，并给出运行结果（输出、返回值）。

当然，大家还可以根据自己的兴趣和需要设计更为高级的功能，例如像 GDB 等调试器一样的打断点、跟踪执行：

<img src="image/image-20251013105127059.png" alt="image-20251013105127059" style="zoom:67%;" />

设计模拟器的可能存在的重难点主要在于维护程序运行的环境。下面是一些简单的思路提示：

(1) 全局变量（含数组）：需要（在读取到 IR 中相应的定义时）动态分配内存空间（并初始化），并维护界限，当程序访问时需检查是否越界；

(2) 函数调用：需要维护函数调用栈，为每个函数维护栈帧，保存其内部的变量、返回位置等信息；

(3) 局部变量（含临时变量）：在执行每个函数时为出现的变量在栈帧上动态分配空间，记录它们的类型和数值，并且能够检查出对未定义变量的访问。

