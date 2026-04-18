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
