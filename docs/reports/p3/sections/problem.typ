== 指令选择中的细节问题

=== 浮点常量编码

RISC-V 指令的立即数只有 12 位，无法直接编码 32 位或 64 位的 IEEE 754 浮点常量。

设有 `float pi = 3.14159;`。`extract_imm()` 对浮点 `ConstexprValue` 会失败，变量未被初始化。

我们将浮点常量放入 `.rodata` 的常量池，通过 Load 加载：

```asm
    la  t0, .L_fc_0
    fld fa0, 0(t0)
    ...
.section .rodata
.balign 8
.L_fc_0:
    .dword 0x4005BF0A8B145769   # 2.71828
```

=== rd == lhs 时的寄存器冲突

二元运算中若目标寄存器与左操作数相同，`PseudoLI` 加载立即数到 `rd` 会覆盖左操作数。

设有 `a = a + 100000;`（`a` 分配在 t6，100000 超出 12 位）。

错误情况：

```asm
li  t6, 100000       # 覆盖了 a 的原始值
add t6, t6, t6       # t6 = 200000，而非 a + 100000
```

修正：`rd == lhs` 时用临时寄存器 `t0`(x5) 加载立即数：

```cpp
bool clobber = (*rd == *lhs);
auto tmp = clobber ? gpr(5) : gpr(*rd);
```

=== 大立即数比较

`SLTI` 的立即数也是 12 位。当比较常数超出此范围时，需要变换。

设有 `if (a > 5000)`（5000 > 2047）。`x > N` 当 N+1 在范围内时等价于 `!(x < N+1)`，用 `SLTI + XORI` 完成。超出则转 `N < x`，走 `LI + SLT`。`x >= N` 等价于 `!(x < N)`，同理。`x == N` 无立即数限制，直接 `LI + SUB + SEQZ`。

=== 数组初始化 Store

IR 中数组的初始值以字节序列存储，需要将其完整 Store 到栈上。简单的 `LI + SW` 只能处理 32 位对齐的情况。

设有 `int a[4] = {1, 2, 3, 4};`。`emit_store_bytes()` 按 4/2/1 字节分块，每块用对应的 Store 宽度（`SW`/`SH`/`SB`）。

=== 浮点-整数类型转换

IR 的 `CONVERT` 指令覆盖了整数-浮点互转和精度升降。RISC-V 有 12 种不同的浮点转换指令。需要根据源和目标的类型精确匹配：

- `float` <-> `double`：`FCVT.S.D` / `FCVT.D.S`
- `float`/`double` -> `int32`：`FCVT.W.S` / `FCVT.W.D`
- `float`/`double` -> `int64`：`FCVT.L.S` / `FCVT.L.D`
- `int32` -> `float`/`double`：`FCVT.S.W` / `FCVT.D.W`
- `int64` -> `float`/`double`：`FCVT.S.L` / `FCVT.D.L`

=== 全局变量 Load/Store

RISC-V 的 Load/Store 使用基址+偏移寻址。全局变量的地址由链接器确定，编译期未知。不能直接 `ld t0, global_a`。

我们用 `la` 伪指令先取地址，再 Load/Store。此模式被封装为 `PseudoL(LGD/LGW/SGD/SGW)`，内部自动生成 `la t0, symbol; ld/sw rd, 0(t0)`。

== 栈帧与寄存器管理

=== 叶子函数的寄存器保存

在叶子函数（不调用其他函数的函数）中，`ra` 和 callee-saved 寄存器不会被修改，但 callee-saved 寄存器的保存/恢复指令仍由 `precolorize` Pass 显式插入在入口和出口。

实际消除这些冗余保存/恢复的机制是隐式的：寄存器分配器通过 coalescing（合并）将保存用的临时变量与对应的寄存器节点合并为同一着色，后续 `RedundantMoveElimination` Pass 会移除源和目标相同的 MOV 指令，从而消除不必要的保存/恢复。

此外，`compute_frame()` 中的 `outgoing_arg_area()` 对叶子函数返回 0，不会为传出参数预留栈空间。

== 降级过程中的问题

=== 干涉图零度节点

寄存器分配构建干涉图时，初始实现只在"变量出现在指令的 used/defined 集合中且与当前 live-out 集合发生冲突"时才将变量加入干涉图。但如果某个变量在 live-out 中出现但从未和任何其他变量冲突（零度节点），它不会被加入图中，后续着色阶段找不到该节点，导致寄存器分配失败。

修正：在扫描指令收集变量时，对每个出现的变量调用 `graph_of(*var).ensure(*var)` 确保其存在于干涉图中，即使度数为零。同时增加 `need_register()` 过滤器，跳过 `reference` 属性和 `comptime` 属性的变量（它们不需要寄存器）。

=== 指令选择的错误处理

指令选择初期用 `if (!rd) return;` 静默跳过无法翻译的指令，导致生成的汇编缺失指令，错误难以定位。后来全部改为 `throw COMPILER_ERROR` 并附带指令的 `toString()` 信息，出问题时可以直接看到是哪条 IR 指令无法翻译及其原因。
