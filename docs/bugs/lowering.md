# Lowering 

## Bugs 

### 一个神秘的观察者效应bug

```bash 
echo 100 | build/compiler test/samples_codegen/tim_sort.cact --optimize --lowering --exec --ir # failed
echo 100 | build/compiler test/samples_codegen/tim_sort.cact --optimize --lowering --exec # successes
lldb -- build/compiler test/samples_codegen/tim_sort.cact --optimize --lowering --exec --ir # successes
```

不打印IR或者在lldb里面启动就不会出错？

检查了一下最后的IR确实不一样，可能是lowering pass写错了而不是VM写错了

在lowering之前的IR是完全相同的。

在 DeSSA 之后的 IR 是完全相同的

确认了是 RegisterAllocation pass的问题

把 unordered_map/set 改成 map/set 就莫名其妙的好了

问题是我开了 ub sanitizer，应该没有 ub 啊？

### Inversion Bug

给叶子函数做寄存器分配的时候，会出现一种情况：明明caller-saved寄存器可用，但是选用了callee-saved寄存器存值，拿caller-saved寄存器当作保存callee-saved寄存器原始值的寄存器，比如拿 t0 存 s0和恢复 s0，表达式计算的时候中间值存在 s0 里面

我觉得根本原因是图着色的时候使用了临时值给 callee-saved 寄存器恢复值 .entry: %0 = s0; .exit: s0 = %0，但是这个恢复值 %0 的生命周期太长了，着色的时候冲突太多，启发式合并不敢把它和 s0 合并，导致在实际给 %0 或者其他值着色的时候，s0 跟 t0 没有区别，随便选了一个，因此会出现拿 t0 给 s0 保存的我称之为 psuedo-spill 的现象

现在采用两个启发式的方法暂时能解决这个问题：

首先给寄存器评级，如果caller寄存器和callee寄存器同时可用，优先用caller的，因为callee最好留给上面说的这些用来保存值的 %0, etc.
然后会出现另一个问题：给 %0 染色的时候，如果 caller 可用也会给它染一个 caller 寄存器 tx，从而产生 tx 和 s0 之间的 copy，因此在染色的时候先判断一下可用颜色当中有没有颜色是 move-related 节点的颜色，如果有，那么就直接用这个颜色