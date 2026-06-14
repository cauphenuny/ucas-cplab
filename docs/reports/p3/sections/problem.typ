== 指令选择中的细节问题

=== LI 伪指令的符号扩展修正

RISC-V 的 `LI` 伪指令拆解为 `LUI` + `ADDI`。当目标常数的低 12 位最高位为 1 时，`ADDI` 会对 12 位立即数做符号扩展，导致最终值偏差。

设有 `int a = 0xABCDE800;`。`LI t0, 0xABCDE800` 展开后，`ADDI` 将 `0x800` 符号扩展为 `-0x800`，最终 `t0` 的值为 `0xABCDE800 + (-0x1000) = 0xABCDE000`，偏了 0x1000。

修正方法：

```cpp
int32_t lo12 = imm32 & 0xFFF;
if (lo12 & 0x800) {
    hi20 += 1;           // LUI 多加载 0x1000
    lo12 -= 0x1000;      // ADDI 减去 0x1000 抵消
}
```

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

=== 103_stack_spill：高寄存器压力 (O0)

`103_stack_spill.cact` 在 `main` 中声明了 32 个局部变量（`x0` 到 `x31`），每个需要独立存储。O0 下寄存器分配无优化，当寄存器耗尽后需要将变量 spill 到栈上。但某个溢出变量的栈地址计算出现了偏差，读取到了未初始化的栈数据。

O1 下，32 个变量的累加和 `0+1+...+31 = 496` 在编译期被常量折叠，不再需要任何栈变量，测试完全通过。对于需要在 O0 下保持正确性的情况，需要修复栈帧对齐计算中变量排序导致的偏移不一致。

=== 叶子函数的寄存器保存

在叶子函数（不调用其他函数的函数）中，`ra` 和 callee-saved 寄存器不会被修改，但初始实现仍然为它们分配了栈空间并执行保存/恢复。

`compute_frame()` 中增加了叶子函数判断。叶子函数省略 `ra` 和 callee-saved 的保存/恢复，减小栈帧。
