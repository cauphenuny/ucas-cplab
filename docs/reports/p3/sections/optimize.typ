=== IR 优化

我们实现了两级优化管线：在 IR 降级过程中复用 P2 的 Pass 框架，以及在汇编代码生成后进行低层指令模式优化。

除去 P2 中已经实现的优化外，我们实现了算术强度削减，将乘法中乘数为 2 的幂的情况替换为移位指令。

由于我们是在IR内部进行降级，因此降级前和降级后都可以应用为IR实现的优化

降级后使用的优化如下：

```cpp
std::vector<std::pair<std::unique_ptr<NonSSAPass>, std::string>> passes;
if (optimize_alloc)
    passes.emplace_back(std::make_unique<DeadAllocElimination<Context>>(),
                        "Dead Allocation Elimination");
if (optimize_temp)
    passes.emplace_back(std::make_unique<DeadTempElimination<Context>>(),
                        "Dead Temporary Elimination");
if (optimize_block) {
    passes.emplace_back(std::make_unique<DeadBlockElimination<Context>>(),
                        "Dead Block Elimination");
    passes.emplace_back(std::make_unique<SimplifyCFG<Context>>(),
                        "CFG Simplification");
}
while (apply(program, ctx, passes));
```

==== 死变量消除

收集程序中所有被 `NamedValue` 或 `SSAValue` 引用的 `Alloc*`，将不再被任何指令引用的 `Alloc` 从 `Func.locals()` 中移除。Phi 消除和寄存器分配后经常产生不再使用的变量绑定，此 Pass 将它们释放，减小栈帧。

==== 死临时变量消除

扫描所有指令的使用链，删除未被使用的 `TempValue`。与死变量消除配合，有效压缩栈帧大小。寄存器分配可能在寄存器间产生仅用作传递的临时值，后续不再被读取。

==== 死基本块消除

从入口块出发做可及性分析（DFS），将不可达的基本块及其内部指令全部删除，同时精简 Phi 指令中指向不可达块的分支。CFG 简化（如条件分支常量折叠）后经常产生不可达块。此 Pass 迭代执行——删除不可达块可能使更多块变得不可达。

==== CFG 简化

两种操作。Squash（块折叠）：如果一个基本块只有 `JumpExit` 且无指令，其前驱中的跳转目标直接替换为该块的目标，同时更新 Phi 指令中的块引用。Redirect（入口重定向）：若入口块只有 `JumpExit` 且目标块无 Phi，则将目标块的内容合并到入口块。

=== 汇编代码生成后的优化

==== 死代码消除

寄存器分配和 CFG 简化后，汇编层面仍然可能产生冗余指令。跳转到只包含跳转指令的块可以被折叠：

```asm
// 优化前:                 // 优化后:
    j   .L_intermediate        j   .L_final
.L_intermediate:           .L_final:
    j   .L_final               ...
.L_final:
    ...
```

此外，不再被任何跳转引用的标签可以从输出中移除（指令序列保留，因为可能通过 fall-through 执行）。

==== 窥孔优化

对特定指令模式做局部替换。比较 `SLTI`/`XORI` 组合中若立即数为 0，可简化为 `SEQZ`/`SNEZ`。

=== O0 与 O1 对比

以 `069_greatest_common_divisor.cact`（欧几里得算法）为例。O0 下 `fun` 作为独立函数，`main` 通过 `call fun` 调用，含完整的栈帧保存恢复，共 58 行。O1 下 `fun` 被内联到 `main` 中，死变量消除移除未使用的中间值，CFG 简化合并冗余基本块，共 41 行，缩减约 29%。

O1 的核心循环（内联在 `main` 中）：

```asm
.L_main_inline_fun_0_while_cond_3_1:
    slti t5, t6, 1
    xori t5, t5, 1
    bne  t5, zero, .L_main_inline_fun_0_while_body_3_1
    j    .L_main_inline_fun_0_return
.L_main_inline_fun_0_while_body_3_1:
    rem  t5, a0, t6
    addi a0, t6, 0
    addi t6, t5, 0
    j    .L_main_inline_fun_0_while_cond_3_1
```

对比 O0 的独立 `fun` 函数版本，内联消除了 `call`/`ret` 和参数通过寄存器 `a0`/`a1` 来回传递的开销。O0 中 `main` 需要先把 `get_int` 的返回值保存到 `s0`、设置 `a0`/`a1`、再 `call fun`；O1 直接在 `main` 的上下文中操作变量。
