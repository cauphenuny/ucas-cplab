=== IR 优化

我们实现了两级优化管线：在 IR 降级过程中复用 P2 的 Pass 框架，以及在汇编代码生成后进行低层指令模式优化。

除去 P2 中已经实现的优化外，我们实现了算术强度削减，将乘法中乘数为 2 的幂的情况替换为移位指令。

由于我们是在IR内部进行降级，因此降级前和降级后都可以应用为IR实现的优化

降级后使用的优化如下（由 `--optimize-lowering` 守卫，该 flag 为 level-2，`-O1` 不启用）：

```cpp
std::vector<std::pair<std::unique_ptr<NonSSAPass>, std::string>> passes;
if (optimize_lower) {
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
}
// ConstantFolding 始终运行，因为 RISC-V 汇编 I 型指令只能接受一个立即数操作数
passes.emplace_back(std::make_unique<ConstantFolding<Context>>(),
                    "Constant Folding");
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

汇编层面共实现了四个优化 Pass：

==== 冗余跳转消除 (RedundantJumpElimination)

寄存器分配和 CFG 简化后，汇编层面仍然可能产生冗余指令。跳转到只包含跳转指令的块可以被折叠：

```asm
// 优化前:                 // 优化后:
    j   .L_intermediate        j   .L_final
.L_intermediate:           .L_final:
    j   .L_final               ...
.L_final:
    ...
```

此外，紧接在下一个基本块标签前的无条件跳转（`j next_label`）也可以直接删除，因为 fall-through 自然到达。

==== 死标签消除 (DeadLabelElimination)

不再被任何跳转引用的标签可以从输出中移除。该标签下的指令序列合并到前一个基本块中（因为可能通过 fall-through 执行）。

==== 分支条件简化 (BranchCondSimplification)

三条 Peephole 规则对比较 + 分支模式进行简化：

1. `SLT/SLTI/SEQZ + XORI x,x,1 + BNEZ/BNEQ x, L` → `CMP + BEQZ/BNEZ x, L`（翻转分支条件，省略 XORI）
2. `SLT/SLTU x, y, z + BNEZ/BEQZ x, L` → `BLT/BGE/BLTU/BGEU y, z, L`（合并比较和分支为单条 RISC-V 分支指令）
3. `SEQZ/SNEZ x, y + BNEZ/BEQZ x, L` → `BEQZ/BNEZ y, L`（直接对原始操作数分支）

==== 冗余 Load 消除 (RedundantLoadElimination)

若 Store 指令后紧跟对同一地址相同偏移的 Load，用位操作提取 Store 值替代 Load：

```asm
// 优化前:                     // 优化后:
    sw  a0, 8(sp)                  sw   a0, 8(sp)
    lw  t0, 8(sp)                  addiw t0, a0, 0
```

=== O0、O1 与 O2 对比

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
