#import "../preamble/preamble.typ": *
#show: doc => conf(doc, title: "P2 实验报告")

= IR 设计

== IR 结构

我们的中间表示 RIIR (acronym of "#text(red)[#strong[R]]IIR #text(red)[#strong[I]]s an #text(red)[#strong[I]]ntermediate #text(red)[#strong[R]]epresentation") 参考了 LLVM IR 的设计和 Rust 的语法，结构如下：

#split

- 程序 `Program` 由若干全局变量 (`Alloc`) 和若干函数 (`Func`) 组成。
- 函数 `Func` 由若干参数 (`Alloc`) 、若干局部变量 (`Alloc`) 和若干基本块 (`Block`) 组成，入口是第一个基本块。
- 内置函数 `BuiltinFunc` 由一个名字和一个类型组成。
- 基本块 `Block` 由若干 指令 (`Inst`) 和一个出口 (`Exit`) 组成。

- 出口 `Exit` 可以是 无条件跳转 (`JumpExit`)、条件跳转 (`BranchExit`) 或 返回 (`Return`)。

#split

- 变量 `Alloc` 包含名字、类型和属性，属性为 `comptime`、`immutable`、`reference`：
  - `comptime` 表示值在编译期已知 （comptime 一定是 immutable的）；
  - `immutable` 表示运行时单赋值；
  - `reference` 表示名字绑定的是引用，读取/写入分别通过 `LOAD`/`STORE` 完成。
  为便于后续 SSA 转换，全局变量统一标记为 `reference`。

#split

- 值 `Value` 是一个 variant，包含3个 alternative：`NamedValue`, `TempValue`, `ConstexprValue`

- 左值 `LeftValue` 是一个 variant，包含2个 alternative：`NamedValue`, `TempValue`
- 临时值 `TempValue` 包含类型和数字id，数字 id 的作用域是函数，同一个函数不能出现对两个对相同 id 临时值的赋值
- 具名值 `NamedValue` 包含类型和一个指向一个 `Alloc`, `Func` 或者 `BuiltinFunc` 的指针
- 常量值 `ConstexprValue` 包含类型以及一个常量值或数组buffer

#split

- 指令 `Inst` 是一个 variant，包含3个 alternative：`UnaryInst`, `BinaryInst`, `CallInst`

- 一元指令 `UnaryInst` 包含一个操作符和一个左值（结果）和一个值（操作数）

#align(center)[
  #three-line-table[
    | 一元指令 | 作用 | 示例 |
    | --- | --- | --- |
    | `LOAD` | 从引用读取值（解引用） | `$1: int = *($0);` |
    | `STORE` | 将值写入引用指向的位置 | `*($1) = $0;` |
    | `MOV` | 赋值 | `$1: int = $0;` |
    | `NOT` | 逻辑非 | `$1: bool = !$0;` |
    | `NEG` | 算术取负 | `$1: int = -$0;` |
    | `BORROW` | 取只读引用 | `$1: &int = &$0;` |
    | `BORROW_MUT` | 取可变引用 | `$1: &mut int = &mut $0;` |
  ]
]

- 二元指令 `BinaryInst` 包含一个操作符和一个左值（结果）和两个值（操作数）

#align(center)[
  #three-line-table[
    | 二元指令 | 作用 | 示例 |
    | --- | --- | --- |
    | `ADD` | 加法 | `$2: int = $0 + $1;` |
    | `SUB` | 减法 | `$2: int = $0 - $1;` |
    | `MUL` | 乘法 | `$2: int = $0 * $1;` |
    | `DIV` | 除法 | `$2: int = $0 / $1;` |
    | `MOD` | 取模 | `$2: int = $0 % $1;` |
    | `LT` | 小于比较 | `$2: bool = $0 < $1;` |
    | `GT` | 大于比较 | `$2: bool = $0 > $1;` |
    | `LEQ` | 小于等于比较 | `$2: bool = $0 <= $1;` |
    | `GEQ` | 大于等于比较 | `$2: bool = $0 >= $1;` |
    | `EQ` | 相等比较 | `$2: bool = $0 == $1;` |
    | `NEQ` | 不等比较 | `$2: bool = $0 != $1;` |
    | `AND` | 逻辑与 | `$2: bool = $0 && $1;` |
    | `OR` | 逻辑或 | `$2: bool = $0 || $1;` |
    | `LOAD` | 数组/切片索引读取 | `$2: int = $0[$1];` |
    | `STORE` | 数组/切片索引写入 | `$2[$0] = $1;` |
  ]
]

- 调用指令 `CallInst` 包含一个左值（结果）、一个具名值（函数）和一个值列表（参数）

#split-full

一段 IR 代码示例如下：


```rust
let ref mut c_0: i32 = 2;

fn foo(a_0: &mut[i32], b_0: &mut[i32]) -> i32 {
.entry:
  $0: i32 = a_0[0];
  $1: i32 = b_0[0];
  $2: i32 = $0 + $1;
  return $2;
}

fn main() -> i32 {
  let mut a_1: [[i32; 2]; 2];
  const b_1: i32 = 1;
.entry:
  a_1: [[i32; 2]; 2] = {1, 2, 4, 0};
  $0: i32 = *(c_0);
  $1: bool = b_1 < $0;
  branch $1 ? if_true_10_4 : if_exit_10_4;
.if_true_10_4:
  $2: &mut[i32] = a_1[0];
  $3: &mut[i32] = a_1[1];
  $4: i32 = foo($2, $3);
  jump if_exit_10_4;
.if_exit_10_4:
  return 0;
}
```

对应原代码

```c
int c = 2;

int foo(int a[2], int b[2]) {
    return a[0] + b[0];
}

int main() {
    int a[2][2] = { {1, 2}, {4} };
    const int b = 1;
    if (b < c) {
        foo(a[0], a[1]);
    }
    return 0;
}
```

== 类型系统

- 基本类型 `Primitive = std::variant<Int, Bool, Double, Bool>`，其中 `Int`、`Float`、`Double`、`Bool` 四个空类分别对应整数 `i32`、单精度浮点数 `f32` 、双精度浮点数 `f64` 和布尔值 `bool`。
- 和类型 `Sum` (e.g. `(i32 | float)` ) 包含一个类型列表，语义类似 `std::variant`
- 积类型 `Product` ( e.g. `(i32, float)` ) 包含一个类型列表，语义类似 `std::tuple`，注意 C 语言中的 `void` 对应空积类型 `()`。
- Top 类型 `Top` ( a.k.a. `⊤` ) 是所有类型的超类型，任何类型都是 `Top` 的子类型。
- Bottom 类型 `Bottom` ( a.k.a. `⊥` ) 是所有类型的子类型，任何类型都是 `Bottom` 的超类型。

- 函数类型 `Func` ( e.g. `(i32, float) -> float` ) 包含参数类型（保证为 `Product`）和返回类型。
- 数组类型 `Array` ( e.g. `[i32; 10]` ) 包含元素类型和长度信息
- 指针类型 `Reference` ( e.g. `&[i32]` (readonly), `&mut[i32]` (non-readonly) ) 包含目标类型和是否只读的flag

实现层面，我们使用 `using Type = std::variant<Primitive, Sum, Product, Top, Bottom, Func, Array, Reference>` 来表示类型，同时创建 `TypeBox` 类包含一个 `Type` 提供一些方便的方法，如 `is<T>(), as<T>(), flatten(), decay()`

=== 子类型判断

现在有两个类型 from 和 to，from 和 to 都是上面提到的类的一个对象，下面判断 from 代表的类型是否是 to 代表的类型的子类型：

- 对于 Primitive 类型中的任意两个 alternative type，只有当它们完全相同时才是子类型关系，例如 `i32` 不是 `f32` 的子类型，反之亦然。

  ```cpp
  template <typename T, typename = std::enable_if_t<is_primitive_v<T>>>
  bool operator<=(const T&, const T&) {
      return true;  // same type is always a subtype
  }
  ```

  其中 `is_primitive_v<T>` 是一个编译期常量表达式，用于判断类型 `T` 是否是 Primitive 类型中的一个 alternative type：

  ```cpp
  template <typename T> struct is_primitive : std::false_type {};
  template <> struct is_primitive<Int> : std::true_type {};
  template <> struct is_primitive<Float> : std::true_type {};
  template <> struct is_primitive<Double> : std::true_type {};
  template <> struct is_primitive<Bool> : std::true_type {};

  template <typename T>
  inline constexpr bool is_primitive_v = is_primitive<T>::value;
  ```

- 对于 Primitive 类型，将其转换成对于 active alternative type 的比较

  ```cpp
  template <typename T, typename = std::enable_if_t<is_primitive_v<T>>>
  bool operator<=(const Primitive& from, const T& to) {
      return Match{from}([&](const auto& from) -> bool { return from <= to; });
  }

  template <typename T, typename = std::enable_if_t<is_primitive_v<T>>>
  bool operator<=(const T& from, const Primitive& to) {
      return Match{to}([&](const auto& to) -> bool { return from <= to; });
  }

  inline bool operator<=(const Primitive& from, const Primitive& to) {
      return Match{from, to}([](const auto& from, const auto& to) -> bool { return from <= to; });
  }
  ```

- 对于 Product 类型，from 是 to 的子类型当且仅当 from 和 to 的元素个数相同，并且 from 中的每个元素都是 to 中对应位置元素的子类型

  代码略

- 如果 from 是 Sum，则 from 是 to 的子类型当且仅当 from 中的每个元素都是 to 的子类型；否则如果 to 是 Sum，则 from 是 to 的子类型当且仅当 from 是 to 中的某个元素的子类型

  ```cpp
  template <typename T>
  std::enable_if_t<!std::disjunction_v<std::is_same<T, TypeBox>, std::is_same<T, Type>>, bool>
  operator<=(const Sum& from, const T& to) {
      return std::all_of(from.items_.begin(), from.items_.end(),
                        [&](const TypeBox& item) { return item <= to; });
  }

  inline bool operator<=(const Sum& from, const Sum& to) {  // forall T in from s.t. T -> to
      return std::all_of(from.items_.begin(), from.items_.end(),
                        [&](const TypeBox& item) { return item <= to; });
  }

  template <typename T>
  std::enable_if_t<!std::disjunction_v<std::is_same<T, TypeBox>, std::is_same<T, Type>>, bool>
  operator<=(const T& from, const Sum& to) {
      return std::any_of(to.items_.begin(), to.items_.end(),
                        [&](const TypeBox& item) { return from <= item; });
  }
  ```

- 如果都是 Func 类型，from 是 to 的子类型当且仅当 from 的参数类型是 to 的参数类型的超类型（contravariance），并且 from 的返回类型是 to 的返回类型的子类型（covariance）

  ```cpp
  inline bool operator<=(const Func& from, const Func& to) {
      return (to.params <= from.params) &&  // contravariance
             (from.ret <= to.ret);          // covariance
  }
  ```

- 如果都是 Array 类型，from 是 to 的子类型当且仅当 from 和 to 的元素类型相同，并且 from 的长度等于 to 的长度

  ```cpp
  inline bool operator<=(const Array& from, const Array& to) {
      if (!(from.elem <= to.elem)) return false;
      if (!(to.elem <= from.elem)) return false;
      return from.size == to.size;
  }
  ```

- 如果都是 Reference 类型，from 是 to 的子类型当且仅当以下条件同时满足：

1. from 的只读属性不比 to 更宽松（即 from 不能是 `&mut` 而 to 是 `&`）
  2. from 的目标类型是 to 的目标类型的子类型  (covariance)
  3. 若 to 非只读，则 from 的目标类型与 to 的目标类型相同 (invariance)
  
  实现上，目标类型指的是 `{from, to}.elem`#text(red)[`.decay()`] // TODO: 改成没有decay，把bug解释一下
  
  ```cpp
  inline bool operator<=(const Reference& from, const Reference& to) {
      auto from_elem = from.elem.decay(from.readonly);
      auto to_elem = to.elem.decay(to.readonly);
      if (!(from_elem <= to_elem)) return false;
      if (from.readonly && !to.readonly) return false;
      if (!to.readonly && !(to_elem <= from_elem)) return false;
      return true;
  }
  ```

- 如果 from 是 Array，并且 to 是 Pointer，则 from 是 to 的子类型当且仅当 from 产生的 可变 Reference 是 to 的子类型

```cpp
inline bool operator<=(const Array& from, const Reference& to) {
    return from.decay(/*readonly=*/false) <= to;
}
```

- 对于其他情况，若 from 是 `Bottom` 或者 to 是 `Top`，则 from 是 to 的子类型，否则不是

  ```cpp
  template <typename T1, typename T2> bool operator<=(const T1& from, const T2& to) {
      if constexpr (std::is_same_v<T1, Bottom>) return true;
      if constexpr (std::is_same_v<T2, Top>) return true;
      return false;
  }
  ```

=== 大小计算

这里的类型大小是在编译器/解释器端虚拟环境中的大小，而不是目标机器上的类型大小（但是实际上没什么区别？）

- 对于普通的 Primitive 类型，我们使用 sizeof(T)

```cpp
template<typename T, typename = std::enable_if_t<is_primitive_v<T>>>
inline size_t size_of(const T&) {
    return sizeof(typename T::type);
}
inline size_t size_of(const Primitive& prim) {
    return Match{prim}([](const auto& t) { return size_of(t); });
}
```

- 对于 Product 类型，大小是所有元素大小之和

```cpp
inline size_t size_of(const Product& prod) {
    size_t size = 0;
    for (const auto& item : prod.items()) {
        size += size_of(item);
    }
    return size;
}
```

- 对于 Sum 类型，大小是所有元素大小的最大值再加上tag的大小（int）

```cpp
inline size_t size_of(const Sum& sum) {
    size_t max_size = 0;
    for (const auto& item : sum.items()) {
        auto item_size = size_of(item);
        if (item_size > max_size) {
            max_size = item_size;
        }
    }
    return max_size + sizeof(int);  // tag
}
```

- 对于 Func 类型或者 Reference 类型，大小是指针大小

```cpp
inline size_t size_of(const Func&) {
    return sizeof(void*);  // function pointer
}
inline size_t size_of(const Reference&) {
    return sizeof(void*);
}
```

- 对于 Array 类型，大小是元素大小乘以长度

```cpp
inline size_t size_of(const Array& arr) {
    return size_of(arr.elem) * arr.size;
}
```

= 语法分析

= IR 生成

= IR 解释执行
