#import "@preview/tablem:0.3.0": three-line-table

== 功能测试

使用 `test/samples_codegen_functional` 中的 75 个测试用例进行功能正确性验证。测试流程为：CACT 编译器将 `.cact` 编译为 RISC-V 汇编 `.s`，`riscv64-unknown-elf-gcc` 汇编并链接 `libcactio.a`，`spike pk` 执行，捕获标准输出和返回值，与参考输出对比。

其中 `100-103` 为额外编写的边界用例：`100_many_args` 测试多参数（23 个，7 个走栈）混合 int/float/double/bool 类型的 ABI；`101_large_array_init` 测试大数组初始化；`102_large_stack` 测试栈帧偏移超出 12 位立即数范围的大栈场景；`103_stack_spill` 测试 32 个局部变量强制寄存器溢出的高寄存器压力场景。

=== O0 结果

75/75 全部通过，与参考输出完全一致。

=== O1 结果

75/75 全部通过。O1 启用 Pre-lowering 优化 Pass（常量折叠、复制传播、死定义消除、死变量消除、死块消除、CFG 简化、CSE、内联展开），所有测试输出和返回值均与参考一致。

=== O2 结果

75/75 全部通过。O2 在 O1 基础上进一步启用降级后优化（`--optimize-lowering`，含死变量/死临时/死基本块消除）和汇编级优化（`--optimize-assembly`，含冗余跳转消除、死标签消除、分支条件简化、冗余 Load 消除）。

== 汇编输出展示

=== O0 与 O1：最大公约数

以 `069_greatest_common_divisor.cact` 为例。CACT 源码：

```c
int fun(int m_0, int n_0) {
    while (n_0 > 0) {
        int rem_0 = m_0 % n_0;
        m_0 = n_0;
        n_0 = rem_0;
    }
    return m_0;
}
int main() {
    int m_1 = get_int();
    int n_1 = get_int();
    int num_0 = fun(m_1, n_1);
    print_int(num_0);
    return 0;
}
```

O0 汇编（58 行）中 `fun` 和 `main` 各自独立。`fun` 拥有完整的 while 循环基本块，`main` 通过 `call fun` 调用，显式保存恢复 `ra` 和 `s0`：

```asm
fun:
    j .Lfun_entry
.Lfun_entry:
    li t1, 0
    j .Lfun_while_cond_3_1
.Lfun_while_cond_3_1:
    slti t1, a1, 1
    xori t1, t1, 1
    bnez t1, .Lfun_while_body_3_1
    j .Lfun_while_exit_3_1
.Lfun_while_exit_3_1:
    j .Lfun_epilogue
.Lfun_while_body_3_1:
    remw t1, a0, a1
    mv a0, a1
    mv a1, t1
    j .Lfun_while_cond_3_1
.Lfun_epilogue:
    ret

main:
    addi sp, sp, -32
    sd ra, 24(sp)
    sd s0, 16(sp)
    call get_int
    mv s0, a0
    call get_int
    mv a1, a0
    mv a0, s0
    call fun
    call print_int
    ...
```

O1 汇编（43 行，缩减约 26%）：`fun` 被内联到 `main` 中，不再有独立的 `fun` 函数。while 循环直接嵌入 `main`，使用 `s0` 暂存第一个 `get_int` 的返回值，避免栈上 spill：

```asm
main:
    addi sp, sp, -32
    sd ra, 16(sp)
    sd s0, 8(sp)
    call get_int
    mv s0, a0
    call get_int
    mv t1, a0
    mv a0, s0
    j .Lmain_inline_fun_0_while_cond_3_1
.Lmain_inline_fun_0_while_cond_3_1:
    slti t2, t1, 1
    xori t2, t2, 1
    bnez t2, .Lmain_inline_fun_0_while_body_3_1
    j .Lmain_inline_fun_0_return
.Lmain_inline_fun_0_return:
    call print_int
    li a0, 0
    ld s0, 8(sp)
    ld ra, 16(sp)
    j .Lmain_epilogue
.Lmain_inline_fun_0_while_body_3_1:
    remw t2, a0, t1
    mv a0, t1
    mv t1, t2
    j .Lmain_inline_fun_0_while_cond_3_1
.Lmain_epilogue:
    addi sp, sp, 32
    ret
```

=== O2：进一步压缩

O2 在 O1 基础上启用汇编级优化。`slti + xori + bnez` 被 BranchCondSimplification 合并为 `slti + beqz`，返回块与条件块合并，消除了中间标签和跳转，从 O1 的 43 行压缩到 37 行：

```asm
main:
    addi sp, sp, -32
    sd ra, 16(sp)
    sd s0, 8(sp)
    call get_int
    mv s0, a0
    call get_int
    mv t1, a0
    mv a0, s0
.Lmain_inline_fun_0_while_cond_3_1:
    slti t2, t1, 1
    beqz t2, .Lmain_inline_fun_0_while_body_3_1
    call print_int
    li a0, 0
    ld s0, 8(sp)
    ld ra, 16(sp)
    j .Lmain_epilogue
.Lmain_inline_fun_0_while_body_3_1:
    remw t2, a0, t1
    mv a0, t1
    mv t1, t2
    j .Lmain_inline_fun_0_while_cond_3_1
.Lmain_epilogue:
    addi sp, sp, 32
    ret
```

=== O1 更深示例：背包问题

`072_backpack.cact` 实现 0-1 背包的动态规划。O1 下生成 212 行汇编，含双重循环和数组访问。

== 动态指令数

以内置 RISC-V 虚拟机统计 O0、O1、O2 的动态指令数，以 `069_greatest_common_divisor`（输入 `1071 462`）为例：

#figure(
  table(
    columns: 4,
    [模式], [汇编行数], [动态指令数], [缩减],
    [O0], [58], [50], [-],
    [O1], [43], [40], [~20%],
    [O2], [37], [33], [~34%],
  ),
  caption: "各优化级别的行数与动态指令数对比",
)

O1 的减少来自函数内联消除 call/ret 开销和死代码消除。O2 进一步通过降级后 CFG 简化和汇编级窥孔优化压缩指令。

== 性能测试

使用 `test/samples_codegen` 中的三个 benchmark（`fib_subseq`、`matrix`、`tim_sort`）在 Spike 上统计执行 ticks，与 `riscv64-unknown-elf-gcc -O0` 对比：

#figure(
  text(size: 0.9em, three-line-table[
    | Benchmark | Opt | CACT ticks | CACT cycles | CACT instrs | gcc ticks | gcc cycles | gcc instrs |
    |-----------|-----|-----------|-------------|-------------|-----------|------------|------------|
    | fib_subseq | -O0 | 30750 (−17.2%) | 2784639 (−18.8%) | 2784639 (−18.8%) | 37150 | 3429521 | 3429521 |
    | fib_subseq | -O1 | 22350 (−39.8%) | 1949120 (−43.2%) | 1949120 (−43.2%) | 37150 | 3429521 | 3429521 |
    | fib_subseq | -O2 | 17550 (−52.8%) | 1471228 (−57.1%) | 1471228 (−57.1%) | 37150 | 3429521 | 3429521 |
    | matrix | -O0 | 11000 (+1.4%) | 811340 (−0.2%) | 811340 (−0.2%) | 10850 | 812625 | 812625 |
    | matrix | -O1 | 9650 (−11.1%) | 682312 (−16.0%) | 682312 (−16.0%) | 10850 | 812625 | 812625 |
    | matrix | -O2 | 9550 (−12.0%) | 672348 (−17.3%) | 672348 (−17.3%) | 10850 | 812625 | 812625 |
    | tim_sort | -O0 | 8100 (−1.8%) | 451775 (−2.5%) | 451775 (−2.5%) | 8250 | 463483 | 463483 |
    | tim_sort | -O1 | 8150 (−1.2%) | 453058 (−2.2%) | 453058 (−2.2%) | 8250 | 463483 | 463483 |
    | tim_sort | -O2 | 8150 (−1.2%) | 449777 (−3.0%) | 449777 (−3.0%) | 8250 | 463483 | 463483 |
  ]),
  caption: "Spike 性能对比（CACT 括号内为相对 gcc 的变化率）",
)

`fib_subseq` 和 `matrix` 优化后大幅领先 gcc；`tim_sort` 与 gcc 持平。
