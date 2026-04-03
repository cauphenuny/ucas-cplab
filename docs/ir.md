# Note of Intermediate Representation

type.hpp: 含有 `immutable` 属性的 ADT。

- 支持子类型判断
- 支持 constructable 判断
- 支持计算类型大小，这里的大小是 host 而不是 target 机器上的，用于解释器中分配变量内存。
- 支持多维数组展平 `flatten` 和数组转指针 `decay`

A 是 B 的子类型，代表 A 的所有取值都可以在 B 中表示，即 A 可以安全地被赋值给 B

子类型规则 ( `from <: to ?` )：

- immutable 规则：
    - `to.immutable and (not from.immutable) => false`

    TODO: 把 immutable 规则去掉？因为 immutable 变量是可以从 mutable 变量赋值的，只是只能赋值一次，现在的 immutable 更像是编译器常量类型，而不是不可变类型

- 其他规则：
    1. `from is ⊥`: `true`
    2. `to is ⊤`: `true`
    3. `{from, to} is primitive_type`: 类型相同则是子类型
    4. `to is sum_type`: `∃ type in to, from <: type`
    5. `from is sum_type`:  `∀ type in from, type <: to`
    6. `{from, to} is sum_type`: `∀ f in from, ∃ t in to, f <: t`
    7. `{from, to} is func_type`: `(to.params <: from.params) and (from.ret <: to.ret)` # 参数协变，返回值逆变
    8. `{from, to} is pointer_type`:
        `if to.elem.immutable => (from.elem <: to.elem)` # 目标元素是const的则协变
        `else => (from.elem <: to.elem) and (to.elem <: from.elem)` # 目标元素不是 const 则不变 （因为可能通过目标指针修改原指针指向的内容）
    9. `{from, to} is array_type`:
        `if to.size != from.size => false`
        `else => from.decay <: to.decay`
    10. `from is array_type, to is pointer_type`: `from.decay <: to`

---

三种值：`NamedValue`/`TempValue`/`ConstexprValue`

分别对应变量，临时值和编译期常量，都带有类型信息

- `ConstexprValue` 由于需要持有内存（不然 initializer_list 的内存由谁管理？），不能拷贝，只能移动。而这个不能拷贝的特性正好阻止了编译期常量同时作为一个指令的结果操作数和另一个指令的输入操作数。

- 对于多维初始化数组，为了让 VM 好写，`ConstexprValue` 维护的是一整块连续的内存。

- `TempValue` 的编号作用域是函数，因此 VM 中可以和局部变量一起预先分配

从 AST `LVal` 转换成 IR `Value` 时，继承 `LVal` 的类型 (对于数组类型，自动 decay 成指针)

---

程序 `Program` 由全局变量列表 `vector<Alloc>` 和 函数列表 `vector<Func>` 构成，入口是名字为 `main` 的函数

函数 `Func` 由局部变量列表 `vector<Alloc>` 和 基本块列表 `vector<Block>` 构成，入口是第一个基本块 (`.entry`)。

`Block` 由指令列表 `vector<Inst>` 和唯一的一个出口 `Exit` 构成。

- 指令有三种，分别是双地址指令、三地址指令、函数调用指令。其中地址是 `Value`
- 出口有三种，分别是 jump, branch或者return。对于跳转类的出口，使用 `const Block*` 索引目标。

`Alloc` 包含一个 `NamedValue` 和可选的初始值

---

生成 IR 时，维护一个 `Func* func` 用于产生新的基本块，维护一个 `Block* scope` 表示当前在这个基本块中产生指令

对于 expression，生成函数 `gen` 的返回值是 `Value` \
对于 instruction，返回值是 `Block*`，表示当前指令执行完之后，会进入哪个基本块，之后在这个基本块添加指令，若返回的是 `nullptr`，则当前指令一定会 `return`，不存在下一个执行的指令

if-stmt / while-stmt / branch (为了短路) 在需要时产生新的基本块，具体见 `ir/gen/stmt.cpp`

---

IR print:

- 由于函数不可嵌套，打印的函数名字可以不包含定义的位置，而变量需要包含位置
- 即使打印存在重名，逻辑也不会受到影响，因为代码中是通过 IR Node 而不是名字来索引变量/函数的

- 临时值的格式是 `$<id>`
- 变量定义：`let`，函数定义：`fn`，一般语句：`{result}: {type} = {expression}`

示例：

```cpp
double foo(double x[2], double y[2]) {
    return x[0] + y[0];
}

int main() {
    double a[2][2] = { {1.0, 2.0}, {4.5e-2} };
    foo(a[0], a[1]);
    return 0;
}
```

```rust
fn foo(x_1_11: &[f64], y_1_24: &[f64]) -> f64 {
.entry:
  $0: f64 = x_1_11[0];
  $1: f64 = y_1_24[0];
  $2: f64 = $0 + $1;
  return $2;
}

fn main() -> i32 {
  let a_6_11: [[f64; 2]; 2];
.entry:
  a_6_11: [[f64; 2]; 2] = {{1.00000, 2.00000}, {0.0450000, 0.00000}};
  $0: &[f64] = a_6_11[0];
  $1: &[f64] = a_6_11[1];
  $2: f64 = foo($0, $1);
  return 0;
}
```

---

```cpp
int a = 0;
int main() {
    int i = 0;
    int b = 0;
    while(i < 3) {
        int b = 1;
        i = i + b;
    }

    {
        const int b = 2;
    }
    return b;
}
```

```rust
let a_1_4: i32 = 0;

fn main() -> i32 {
  let i_3_8: i32;
  let b_4_8: i32;
  let b_6_12: i32;
  let b_11_18: i32 const;
.entry:
  i_3_8: i32 = 0;
  b_4_8: i32 = 0;
  jump while_cond_5_4;
.while_cond_5_4:
  $0: bool = i_3_8 < 3;
  branch $0 ? while_body_5_4 : while_exit_5_4;
.while_body_5_4:
  b_6_12: i32 = 1;
  $1: i32 = i_3_8 + b_6_12;
  i_3_8: i32 = $1;
  jump while_cond_5_4;
.while_exit_5_4:
  b_11_18: i32 const = 2;
  return b_4_8;
}
```