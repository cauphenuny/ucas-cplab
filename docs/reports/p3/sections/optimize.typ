
=== 降级过程中的优化

复用 P2 实验中完成的功能在降级时以及降级后进行一些优化

```cpp 
std::vector<std::pair<std::unique_ptr<NonSSAPass>, std::string>> passes;
if (optimize_lower) {
    if (optimize_alloc)
        passes.emplace_back(
            std::make_unique<DeadAllocElimination<Context>>(),
            "Dead Allocation Elimination");
    if (optimize_temp)
        passes.emplace_back(
            std::make_unique<DeadTempElimination<Context>>(),
            "Dead Temporary Elimination");
    if (optimize_block) {
        passes.emplace_back(
            std::make_unique<DeadBlockElimination<Context>>(),
            "Dead Block Elimination");
        passes.emplace_back(std::make_unique<SimplifyCFG<Context>>(),
                            "CFG Simplification");
    }
}
while (apply(program, ctx, passes));
```

=== 汇编代码生成后的优化

==== 死代码消除

1. 冗余 `j` 指令消除

2. 冗余 label 消除

==== 窥孔优化

1. 分支条件简化
