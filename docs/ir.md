# Notes of Intermediate Representation

**R**IIR **I**s an **I**ntermediate **R**epresentation

~~(or: Rewrite IR In Rust; or: RIIR Isn't Imitating Rust, ...)~~

---

## 类型系统

`ir/type.hpp`: 一个简单的 ADT（类型不含 comptime/immutable 属性，属性在变量绑定 `Alloc` 上）。

- 支持子类型判断
- 支持 constructable 判断
- 支持计算类型大小，这里的大小是 build 机器上而不是 target 机器上的，用于解释器中分配变量内存。
- 支持多维数组展平 `flatten` 和数组转指针 `decay`

A 是 B 的子类型，代表 A 的所有取值都可以在 B 中表示，即 A 可以安全地被赋值给 B

子类型规则 ( `from <: to ?` )：

1. `from is ⊥`: `true`
2. `to is ⊤`: `true`
3. `{from, to} is primitive_type`: 类型相同则是子类型
4. `to is sum_type`: `∃ type in to, from <: type`
5. `from is sum_type`:  `∀ type in from, type <: to`
6. `{from, to} is sum_type`: `∀ f in from, ∃ t in to, f <: t`
7. `{from, to} is func_type`: `(to.params <: from.params) and (from.ret <: to.ret)` # 参数逆变，返回值协变
8. `{from, to} is pointer_type`:
    `if to.readonly => (from.elem <: to.elem)` # 目标是只读指针则协变
    `else => (from.elem <: to.elem) and (to.elem <: from.elem)` # 目标不是只读则不变
9. `{from, to} is array_type`:
    `if to.size < from.size => false`
    `else => (from.elem <: to.elem) and (to.elem <: from.elem)` # 数组元素不变
10. `from is array_type, to is pointer_type`: `from.decay <: to`

---

## 变量绑定

变量绑定 `Alloc` 有几个属性：
- `comptime`：值在编译期已知（如 `const`）。
- `immutable`：运行时不允许被多次赋值（单赋值）。如果声明时没有 `mut` 修饰，则默认 `immutable = true`。
- `reference`：名字绑定到值的引用而不是值本身；访问值必须通过 `load`/`store` 指令。

两种声明形式和修饰符：
- `const x: type = val;` → `comptime = true` 且 `immutable = true`，一定有 `= constexpr`。可用 `ref` 标注为引用：`const ref x: T = ...`（此时为只读引用）。
- `let x: type;` → 默认 `immutable = true`（不可变），除非写成 `let mut x: type;`。可用 `ref` 标注为引用：`let ref x: T;` 或 `let mut ref x: T;`。

对于 non-comptime 且 `immutable` 的变量，可以用 `TempValue` 表示，不分配变量名字；可变变量仍会分配命名的 `Alloc`。

Reference 的类型与访问：
- 从 `reference` 的 `Alloc` 得到的 `NamedValue` 的类型为 `alloc.type.borrow(readonly: alloc.immutable)`，即 IR 中的 `Reference` 类型（可能带 `is_slice` 标记）。
- 对非 slice 的 `Reference` 读取会在 IR 生成阶段插入 `LOAD` 指令来解引用；赋值会生成 `STORE` 指令而不是普通的 `MOV`。
- 这保证后续 SSA/优化阶段统一通过引用形式访问被标注的变量（尤其是全局变量）。

示例：
```rust
const ref g: int = 42;
let mut ref p: int;
...
// IR 中 g, p 的 NamedValue 类型是 &int, &mut int，对 p 的赋值会生成 STORE p, <value>，对 g 的读取会生成 LOAD tmp, g
```

- 为了便于后续 SSA 转换，所有的全局变量都标记为 `reference`。

---

## IR 中的值

三种值：`NamedValue`/`TempValue`/`ConstexprValue`

分别对应变量，临时值和编译期常量，都带有类型信息

- `ConstexprValue` 由于需要持有内存（不然 initializer_list 的内存由谁管理？），不能拷贝，只能移动。而这个不能拷贝的特性正好阻止了编译期常量同时作为一个指令的结果操作数和另一个指令的输入操作数。

- 对于多维初始化数组，为了让 VM 好写，`ConstexprValue` 维护的是一整块连续的内存。

- `TempValue` 的编号作用域是函数，因此 VM 中可以和局部变量一起预先分配

- `TempValue` 是单赋值的，不允许赋值两次

从 AST 的各种 expression 转换成 IR `Value` 时，继承类型 

---

## IR 程序结构

程序 `Program` 由全局变量列表 `vector<Alloc>` 和 函数列表 `vector<Func>` 构成，入口是名字为 `main` 的函数

函数 `Func` 由局部变量列表 `vector<Alloc>` 和 基本块列表 `vector<Block>` 构成，入口是第一个基本块 (`.entry`)。

`Block` 由指令列表 `vector<Inst>` 和唯一的一个出口 `Exit` 构成。

- 指令有三种，分别是双地址指令、三地址指令、函数调用指令。其中地址是 `Value`


一元指令：

| 一元指令 | 作用 | 示例 |
| --- | --- | --- |
| `LOAD` | 从引用读取值（解引用） | `%1: int = * %0;` |
| `MOV` | 赋值 | `%1: int = %0;` |
| `NOT` | 逻辑非 | `%1: bool = ! %0;` |
| `NEG` | 算术取负 | `%1: int = - %0;` |
| `BORROW` | 取只读引用 | `%1: &int = & %0;` |
| `BORROW_MUT` | 取可变引用 | `%1: &mut int = &mut %0;` |

二元指令：

| 二元指令 | 作用 | 示例 |
| --- | --- | --- |
| `ADD` | 加法 | `%2: int = %0 + %1;` |
| `SUB` | 减法 | `%2: int = %0 - %1;` |
| `MUL` | 乘法 | `%2: int = %0 * %1;` |
| `DIV` | 除法 | `%2: int = %0 / %1;` |
| `MOD` | 取模 | `%2: int = %0 % %1;` |
| `LT` | 小于比较 | `%2: bool = %0 < %1;` |
| `GT` | 大于比较 | `%2: bool = %0 > %1;` |
| `LEQ` | 小于等于比较 | `%2: bool = %0 <= %1;` |
| `GEQ` | 大于等于比较 | `%2: bool = %0 >= %1;` |
| `EQ` | 相等比较 | `%2: bool = %0 == %1;` |
| `NEQ` | 不等比较 | `%2: bool = %0 != %1;` |
| `AND` | 逻辑与 | `%2: bool = %0 && %1;` |
| `OR` | 逻辑或 | `%2: bool = %0 \|\| %1;` |
| `LOAD_ELEM` | 数组/切片索引读取 | `%2: int = %0[%1];` |
| `STORE` | 将值写入引用指向的位置 | `%2: () = %1 <- %0;` |


- 出口有三种，分别是 jump, branch或者return。对于跳转类的出口，使用 `const Block*` 索引目标。

`Alloc` 包含一个名字、类型、属性和可选的初始值

- 尽管数组可能是多维的，IR 中保证只存在扁平的初始化数组，e.g. `let a: [[int; 2]; 3] = {0, 1, 2, 3, 4, 5}`

---

生成 IR 时，维护一个 `Func* func` 用于产生新的基本块，维护一个 `Block* scope` 表示当前在这个基本块中产生指令

对于 expression，生成函数 `gen` 的返回值是 `Value` \
对于 instruction，返回值是 `Block*`，表示当前指令执行完之后，会进入哪个基本块，之后在这个基本块添加指令，若返回的是 `nullptr`，则当前指令一定会 `return`，不存在下一个执行的指令

if-stmt / while-stmt / branch (为了短路) 在需要时产生新的基本块，具体见 `ir/gen/stmt.cpp`

---

## IR 打印

- 由于函数不可嵌套，打印的函数名字可以不包含定义的位置，而变量需要包含位置

- 临时值的格式是 `%{id}`
- 变量定义：`const`/`let`，函数定义：`fn`，一般语句：`{result}: {type} = {expression}`
