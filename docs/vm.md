# Notes of IR Virtual Machine

VM 中通过 View 进行计算

View 是对于内存资源的引用，不持有所有权，包含类型 .type 和地址 .data

注意数组和指针的语义不一样：数组的.data指向的是缓冲区开始，而指针的.data指向的是一个指针，这个指针指向的才是缓冲区

---

操作数通过 `view_of` 实现从 IR 操作数 (三种值) 到 VM 操作数 View 的转换，由于 view 不持有内存，进入函数时就需要分配好所有的局部变量和临时变量

---

实参由被调用者处理，复制到被调用者的栈帧中，因此栈帧包含三部分：自己的实参，局部变量，临时变量。

---

当一个 `Alloc` 被标记为 `reference` 时，VM 为该变量分配的缓冲区末尾额外添加一个指针槽（`sizeof(std::byte*)`）。缓冲区布局（从缓冲区开始）示意：
	`[ actual data (size_of(type)) ][ pointer (sizeof(std::byte*)) ]`

---

---

### Phi 节点的并行执行语义

在 SSA 形式中，基本块开头的所有 Phi 指令在逻辑上是**并行执行**的。这意味着所有 Phi 指令的操作数都应该取自进入该块那一刻（即前驱块结束时）的状态。

#### 曾经存在的漏洞 (Bug)
由于 VM 最初对基本块内的指令是**顺序执行**的，如果一个 Phi 指令的操作数依赖于同一个块内另一个 Phi 指令的结果，就会发生冲突。

例如：
```cpp
$m_0 = phi(..., body: $n_0)
$n_0 = phi(..., body: $1)
```

原始优化过程：

```rust
$ build/compiler --ir --ssa --optimize test/samples_codegen_functional/069_greatest_common_divisor.cact --ssa2temp

Generated IR:
fn fun(m_0: i32, n_0: i32) -> i32 {
    let mut rem_0: i32;
    'entry: {
        rem_0: i32 = 0;
        jump 'while_cond_3_1;
    }
    'while_cond_3_1: {
        $0: bool = n_0 > 0;
        branch $0 ? 'while_body_3_1 : 'while_exit_3_1;
    }
    'while_body_3_1: {
        $1: i32 = m_0 % n_0;
        rem_0: i32 = $1;
        m_0: i32 = n_0;
        n_0: i32 = rem_0;
        jump 'while_cond_3_1;
    }
    'while_exit_3_1: {
        return m_0;
    }
}

fn main() -> i32 {
    let mut n_1: i32;
    let mut m_1: i32;
    let mut num_0: i32;
    'entry: {
        n_1: i32 = 0;
        m_1: i32 = 0;
        num_0: i32 = 0;
        $0: i32 = get_int();
        m_1: i32 = $0;
        $1: i32 = get_int();
        n_1: i32 = $1;
        $2: i32 = fun(m_1, n_1);
        num_0: i32 = $2;
        $3: () = print_int(num_0);
        return 0;
    }
}

SSA Form:
fn fun(m_0: i32, n_0: i32) -> i32 {
    let rem_0: i32;
    'entry: {
        $rem_0.0: i32 = 0;
        jump 'while_cond_3_1;
    }
    'while_cond_3_1: {
        $m_0.0: i32 = $phi('entry: m_0, 'while_body_3_1: $m_0.1);
        $n_0.0: i32 = $phi('entry: n_0, 'while_body_3_1: $n_0.1);
        $0: bool = $n_0.0 > 0;
        branch $0 ? 'while_body_3_1 : 'while_exit_3_1;
    }
    'while_body_3_1: {
        $1: i32 = $m_0.0 % $n_0.0;
        $rem_0.1: i32 = $1;
        $m_0.1: i32 = $n_0.0;
        $n_0.1: i32 = $rem_0.1;
        jump 'while_cond_3_1;
    }
    'while_exit_3_1: {
        return $m_0.0;
    }
}

fn main() -> i32 {
    let n_1: i32;
    let m_1: i32;
    let num_0: i32;
    'entry: {
        $n_1.0: i32 = 0;
        $m_1.0: i32 = 0;
        $num_0.0: i32 = 0;
        $0: i32 = get_int();
        $m_1.1: i32 = $0;
        $1: i32 = get_int();
        $n_1.1: i32 = $1;
        $2: i32 = fun($m_1.1, $n_1.1);
        $num_0.1: i32 = $2;
        $3: () = print_int($num_0.1);
        return 0;
    }
}

SSA Form (TempValue):
fn fun(m_0: i32, n_0: i32) -> i32 {
    let rem_0: i32;
    'entry: {
        $2: i32 = 0;
        jump 'while_cond_3_1;
    }
    'while_cond_3_1: {
        $4: i32 = $phi('entry: m_0, 'while_body_3_1: $3);
        $6: i32 = $phi('entry: n_0, 'while_body_3_1: $5);
        $0: bool = $6 > 0;
        branch $0 ? 'while_body_3_1 : 'while_exit_3_1;
    }
    'while_body_3_1: {
        $1: i32 = $4 % $6;
        $7: i32 = $1;
        $3: i32 = $6;
        $5: i32 = $7;
        jump 'while_cond_3_1;
    }
    'while_exit_3_1: {
        return $4;
    }
}

fn main() -> i32 {
    let n_1: i32;
    let m_1: i32;
    let num_0: i32;
    'entry: {
        $4: i32 = 0;
        $5: i32 = 0;
        $6: i32 = 0;
        $0: i32 = get_int();
        $7: i32 = $0;
        $1: i32 = get_int();
        $8: i32 = $1;
        $2: i32 = fun($7, $8);
        $9: i32 = $2;
        $3: () = print_int($9);
        return 0;
    }
}

Copy Propagation:
fn fun(m_0: i32, n_0: i32) -> i32 {
    let rem_0: i32;
    'entry: {
        $2: i32 = 0;
        jump 'while_cond_3_1;
    }
    'while_cond_3_1: {
        $4: i32 = $phi('entry: m_0, 'while_body_3_1: $6);
        $6: i32 = $phi('entry: n_0, 'while_body_3_1: $1);
        $0: bool = $6 > 0;
        branch $0 ? 'while_body_3_1 : 'while_exit_3_1;
    }
    'while_body_3_1: {
        $1: i32 = $4 % $6;
        $7: i32 = $1;
        $3: i32 = $6;
        $5: i32 = $1;
        jump 'while_cond_3_1;
    }
    'while_exit_3_1: {
        return $4;
    }
}

fn main() -> i32 {
    let n_1: i32;
    let m_1: i32;
    let num_0: i32;
    'entry: {
        $4: i32 = 0;
        $5: i32 = 0;
        $6: i32 = 0;
        $0: i32 = get_int();
        $7: i32 = $0;
        $1: i32 = get_int();
        $8: i32 = $1;
        $2: i32 = fun($0, $1);
        $9: i32 = $2;
        $3: () = print_int($2);
        return 0;
    }
}

Const Propagation:
fn fun(m_0: i32, n_0: i32) -> i32 {
    let rem_0: i32;
    'entry: {
        $2: i32 = 0;
        jump 'while_cond_3_1;
    }
    'while_cond_3_1: {
        $4: i32 = $phi('entry: m_0, 'while_body_3_1: $6);
        $6: i32 = $phi('entry: n_0, 'while_body_3_1: $1);
        $0: bool = $6 > 0;
        branch $0 ? 'while_body_3_1 : 'while_exit_3_1;
    }
    'while_body_3_1: {
        $1: i32 = $4 % $6;
        $7: i32 = $1;
        $3: i32 = $6;
        $5: i32 = $1;
        jump 'while_cond_3_1;
    }
    'while_exit_3_1: {
        return $4;
    }
}

fn main() -> i32 {
    let n_1: i32;
    let m_1: i32;
    let num_0: i32;
    'entry: {
        $4: i32 = 0;
        $5: i32 = 0;
        $6: i32 = 0;
        $0: i32 = get_int();
        $7: i32 = $0;
        $1: i32 = get_int();
        $8: i32 = $1;
        $2: i32 = fun($0, $1);
        $9: i32 = $2;
        $3: () = print_int($2);
        return 0;
    }
}

Dead Definition Elimination:
fn fun(m_0: i32, n_0: i32) -> i32 {
    let rem_0: i32;
    'entry: {
        jump 'while_cond_3_1;
    }
    'while_cond_3_1: {
        $4: i32 = $phi('entry: m_0, 'while_body_3_1: $6);
        $6: i32 = $phi('entry: n_0, 'while_body_3_1: $1);
        $0: bool = $6 > 0;
        branch $0 ? 'while_body_3_1 : 'while_exit_3_1;
    }
    'while_body_3_1: {
        $1: i32 = $4 % $6;
        jump 'while_cond_3_1;
    }
    'while_exit_3_1: {
        return $4;
    }
}

fn main() -> i32 {
    let n_1: i32;
    let m_1: i32;
    let num_0: i32;
    'entry: {
        $0: i32 = get_int();
        $1: i32 = get_int();
        $2: i32 = fun($0, $1);
        $3: () = print_int($2);
        return 0;
    }
}

Dead Allocation Elimination:
fn fun(m_0: i32, n_0: i32) -> i32 {
    'entry: {
        jump 'while_cond_3_1;
    }
    'while_cond_3_1: {
        $4: i32 = $phi('entry: m_0, 'while_body_3_1: $6);
        $6: i32 = $phi('entry: n_0, 'while_body_3_1: $1);
        $0: bool = $6 > 0;
        branch $0 ? 'while_body_3_1 : 'while_exit_3_1;
    }
    'while_body_3_1: {
        $1: i32 = $4 % $6;
        jump 'while_cond_3_1;
    }
    'while_exit_3_1: {
        return $4;
    }
}

fn main() -> i32 {
    'entry: {
        $0: i32 = get_int();
        $1: i32 = get_int();
        $2: i32 = fun($0, $1);
        $3: () = print_int($2);
        return 0;
    }
}
```

如果顺序执行且 `$n_0` 的 Phi 先被处理，那么 `$m_0` 的 Phi 就会拿到 `$n_0` **更新后**的新值，而不是前驱块传来的旧值。由于 Phi 指令的生成顺序通常是不确定的（例如取决于 `unordered_set` 的遍历顺序），这会导致程序运行结果出现随机性错误（即所谓的“概率出问题”）。

#### 解决方案
VM 现在对基本块开头的 Phi 指令做了特殊处理：
1. **采样阶段 (Sampling)**：在执行任何写入操作之前，遍历块首所有的 Phi 指令，解析操作数，并根据其类型大小（`size_of`），将数据深度拷贝到临时缓冲区中。
2. **写回阶段 (Commit)**：采样完成后，再一次性将缓冲区内的数据通过 `assign` 指令写回目标变量的内存位置。

通过这种“先统一读快照，再统一写回”的机制，VM 能够正确模拟并行执行语义，确保了优化后 IR 执行的确定性与正确性。

---

### CSE (公共子表达式消除) 与 VM 存储模型的冲突

#### 问题本质

CSE 优化假设纯 SSA 语义：每个 SSA 版本 (`$k_0.1`, `$k_0.2`, ...) 是**独立的不可变值**。但 VM 的 `view_of` 实现将同一变量的所有 SSA 版本映射到**同一个存储槽** (`frame.vars[ssa.def]`)：

```cpp
// vm.h: view_of
[&](const SSAValue& ssa) -> std::optional<View> {
    auto it = frame.vars.find(ssa.def);  // 按 Alloc* 查找，忽略 version!
    if (it != frame.vars.end()) return it->second;
    return std::nullopt;
}
```

即 `$k_0.1`、`$k_0.2`、`$k_0.3` 在 VM 中读写的都是 `vars[k_0.def]` 这同一块内存。

#### 触发条件

CSE 将常量赋值 MOV 视为”可消除的公共子表达式”。当多个 SSA 变量被赋值为同一常量时，CSE 会将后续的常量加载替换为对第一个变量的引用：

**CSE 前（正确）：**
```
'while_body_12_8: { $k_0.1 = 0; }          // 直接加载常量
'while_body_14_12: { $l_0.1 = 0; }         // 直接加载常量
```

**CSE 后（错误）：**
```
'while_body_12_8: { $k_0.1 = 0; }
'while_body_14_12: { $l_0.1 = $k_0.1; }    // 改为引用 k_0 的存储
```

经过多轮 CSE 迭代，还会形成更长的 MOV 链：`$i_0.1 → $j_0.1 → $k_0.1 → $l_0.1`。

#### Bug 复现（以 066_array_init2.cact 为例）

测试用例：`float A[4][2][2][2]`，四重循环遍历打印。

执行过程：

| 步骤 | 操作 | 期望值 | VM 实际值 | 说明 |
|------|------|--------|-----------|------|
| 1 | `$k_0.1 = 0` | `vars[k_0] = 0` | 0 | 首次正确 |
| 2 | `$l_0.1 = $k_0.1` | 读取 `vars[k_0]` = 0 | 0 | 首次正确 |
| 3 | 内层 l 循环执行完毕 | — | — | l 从 0 递增到 2 退出 |
| 4 | `$k_0.3 = $k_0.2 + 1` | — | `vars[k_0] = 1` | k 递增，**覆盖**了 `vars[k_0]` |
| 5 | `$l_0.1 = $k_0.1`（第二次） | 应读到 0 | **读到 1** | `vars[k_0]` 已被步骤 4 覆写 |

由于 `$k_0.1` 和 `$k_0.3` 共享 `vars[k_0]`，第二次执行 `$l_0.1 = $k_0.1` 时，`vars[k_0]` 已经不是 0 而是被后续版本覆盖的值。

同理，`$j_0.1 = $i_0.1` 中 `vars[i_0]` 也会被 `$i_0.3 = i + 1` 覆盖。

#### 错误输出推演

```
循环变量初始化链: $l_0.1 = $k_0.1, $k_0.1 = $j_0.1, $j_0.1 = $i_0.1

(i=0,j=0,k=0): l 从 0 开始 → 打印 A[0][0][0][0]=1, A[0][0][0][1]=2
(i=0,j=0,k=1): l 从 1 开始（vars[k_0]=1）→ 打印 A[0][0][1][1]=4
(i=0,j=1,k=1): l 从 1 开始 → 打印 A[0][1][1][1]=8
(i=1,j=1,k=1): l 从 1 开始 → 打印 A[1][1][1][1]=16
(i=2): j 从 2 开始（vars[i_0]=2），j<2 不满足，直接退出
(i=3): 同上
(i=4): i<4 不满足，退出

实际输出: 1.0, 2.0, 4.0, 8.0, 16.0  ← 仅 5 个值
期望输出: 1.0, 2.0, ..., 32.0        ← 应有 32 个值
```

#### 根因总结

| | CSE 的假设 | VM 的实际行为 |
|--|-----------|-------------|
| `$k_0.1` | 不可变 SSA 值，永远为 0 | 读取 `vars[k_0]`，可能已被 `$k_0.2`/`$k_0.3` 覆写 |
| MOV 消除 | `= 0` 和 `= $k_0.1` 等价 | 常量 0 始终正确，`$k_0.1` 可能读到脏值 |

本质上是 **CSE 的纯 SSA 语义** 与 **VM 的共享存储模型** 之间的不匹配。与 Phi 节点的并行执行问题同源——都是因为 VM 中同一变量的不同 SSA 版本共享存储位置。

#### 修复方案

在 CSE 的 `UnaryInst` 处理中跳过 MOV 指令：

```cpp
[&](const UnaryInst& unary) -> Inst {
    if (unary.op == UnaryInstOp::LOAD || unary.op == UnaryInstOp::STORE ||
        unary.op == UnaryInstOp::MOV) {
        return unary;  // skip memory access and copies
    }
    uint32_t operand_num = lookup(unary.operand);
    return convert(encode(unary.op), operand_num, 0, unary);
}
```

这同时解决了两个问题：
1. **正确性**：不再产生跨变量的 SSA 引用，避免读取被覆写的共享存储
2. **收敛性**：消除了 MOV 链的级联效应（`$14=$13`, `$15=$14`, ...），CSE 不再需要 7+ 轮迭代
