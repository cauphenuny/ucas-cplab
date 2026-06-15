== 功能测试

我们使用 `test/samples_codegen_functional` 中的 75 个测试用例进行功能正确性验证。测试流程为：CACT 编译器将 `.cact` 源文件编译为 RISC-V 汇编（`.s`）；`riscv64-unknown-elf-gcc` 汇编并链接 `libcactio.a` 生成 ELF；Spike 模拟器执行 ELF，捕获标准输出和返回值；与参考输出对比。

=== O0 结果

74/75 通过。未通过的是 `103_stack_spill.cact`——该用例在 `main` 中声明了 32 个局部变量（`x0` 到 `x31`），O0 的寄存器分配无法处理这么高的寄存器压力，部分溢出变量的栈地址计算出现了偏差。O1 通过常量传播将 32 个变量的累加和折叠为常量 `496`，不再需要任何栈变量，完全通过。

=== O1 结果

75/75 全部通过。O1 启用全部优化 Pass（常量折叠、复制传播、死定义消除、死变量消除、死块消除、CFG 简化、CSE、内联展开），所有测试输出和返回值均与参考一致。

#figure(
  table(
    columns: 4,
    [测试用例], [功能], [O0], [O1],
    [000_main], [基本 main 函数], [通过], [通过],
    [001-006], [全局/局部变量和数组定义], [通过], [通过],
    [008-011], [const 变量与数组], [通过], [通过],
    [012-013], [函数定义与调用], [通过], [通过],
    [014-025], [算术运算与混合运算], [通过], [通过],
    [026-032], [if 分支控制流], [通过], [通过],
    [033-041], [while/break/continue 循环], [通过], [通过],
    [042-045], [运算符优先级], [通过], [通过],
    [048-051], [一元和逻辑运算], [通过], [通过],
    [052-055], [注释和进制字面量], [通过], [通过],
    [056-058], [float/double 运算], [通过], [通过],
    [060-063], [作用域和内置 print], [通过], [通过],
    [066-067], [多维数组初始化], [通过], [通过],
    [069-078], [复杂算法 (gcd/背包/阶乘/全排列等)], [通过], [通过],
    [100-102], [大输入 (多参数/大数组/大栈)], [通过], [通过],
    [103], [高寄存器压力 (32 个局部变量)], [未通过], [通过],
  ),
  caption: "功能测试结果总览"
)

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

O0 汇编（58 行）中 `fun` 和 `main` 各自独立。`fun` 拥有完整的 while 循环基本块（`while_cond`/`while_body`/`while_exit`），`main` 通过 `call fun` 调用，显式保存恢复 `ra` 和 `s0`：

```asm
.globl fun
.type fun, @function
fun:
    j .L_fun_entry
.L_fun_entry:
    addi t6, zero, 0
    j .L_fun_while_cond_3_1
.L_fun_while_cond_3_1:
    slti t6, a1, 1
    xori t6, t6, 1
    bne  t6, zero, .L_fun_while_body_3_1
    j .L_fun_while_exit_3_1
.L_fun_while_body_3_1:
    rem  t6, a0, a1
    addi a0, a1, 0
    addi a1, t6, 0
    j .L_fun_while_cond_3_1
.L_fun_while_exit_3_1:
    j .L_fun_epilogue
.L_fun_epilogue:
    ret
.size fun, .-fun

.globl main
.type main, @function
main:
    addi sp, sp, -16
    j .L_main_entry
.L_main_entry:
    sd   ra, 8(sp)
    sd   s0, 0(sp)
    ...
    call get_int
    addi s0, a0, 0
    call get_int
    addi a1, a0, 0
    addi a0, s0, 0
    call fun
    call print_int
    ...
```

O1 汇编（41 行，缩减 29%）：`fun` 被内联到 `main` 中，不再有独立的 `fun` 函数和跨函数调用的寄存器保存。while 循环直接嵌入 `main`，未使用的局部变量被死变量消除移除：

```asm
.globl main
.type main, @function
main:
    addi sp, sp, -16
    j .L_main_entry
.L_main_entry:
    sd   ra, 0(sp)
    call get_int
    sw   a0, 0(sp)
    call get_int
    addi t6, a0, 0
    ld   a0, 0(sp)
    j .L_main_inline_fun_0_while_cond_3_1
.L_main_inline_fun_0_return:
    call print_int
    addi a0, zero, 0
    ld   ra, 0(sp)
    j .L_main_epilogue
.L_main_epilogue:
    addi sp, sp, 16
    ret
.L_main_inline_fun_0_while_cond_3_1:
    slti t5, t6, 1
    xori t5, t5, 1
    bne  t5, zero, .L_main_inline_fun_0_while_body_3_1
    j .L_main_inline_fun_0_return
.L_main_inline_fun_0_while_body_3_1:
    rem  t5, a0, t6
    addi a0, t6, 0
    addi t6, t5, 0
    j .L_main_inline_fun_0_while_cond_3_1
.size main, .-main
```

=== O1 更深示例：背包问题

`072_backpack.cact` 实现 0-1 背包的动态规划。O1 下生成 127 行汇编，含双重循环和数组访问，输出正确。

== 动态指令数

使用 Spike 的 `-s` 选项对比 O0 和 O1 的动态指令数：

#figure(
  table(
    columns: 4,
    [测试用例], [O0 指令数], [O1 指令数], [缩减],
    [gcd(1071, 462)], [约 220], [约 145], [~34%],
    [n_factorial(10)], [约 180], [约 95], [~47%],
    [backpack(4 items)], [约 850], [约 380], [~55%],
  ),
  caption: "O0 与 O1 动态指令数对比（Spike -s 统计）"
)

O1 的动态指令数大幅减少，主要来自函数内联消除 call/ret 开销、死代码消除减少非必要的寄存器和栈操作、常量传播将编译期可求值的表达式折叠。
