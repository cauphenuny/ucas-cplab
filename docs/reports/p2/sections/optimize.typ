#import "../../preamble/preamble.typ": *

== IR 分析

=== 数据流分析框架

我们根据龙书9.3.3节描述的算法实现了一个通用数据流分析框架，支持正向/反向分析，不同的合并函数，不同的边界/传递条件

#theorem(title: "通用数据流框架的迭代解法", supplement: "Algorithm")[
  输入：一个由下列部分组成的数据流框架：
  
  + 一个数据流图，有两个被特别标记为 ENTRY 和 EXIT 的节点
  + 数据流的方向 $D$
  + 一个值集 $V$
  + 一个交汇运算 $and$
  + 一个函数的集合 $F$ 其中 $f_B$ 表示基本块 $B$ 的传递函数
  + $V$ 中的一个常量值 $v_"ENTRY"$ 或者 $v_"EXIT"$，分别表示前向和逆向框架的边界条件
  
  输出：数据流图中每一个节点 $B$ 的 $"IN"[B]$ 和 $"OUT"[B]$，值在 $V$ 中
]

---

实现上来说，我们通过模版参数 `Trait` 控制上述每个部分的具体实现，从而实例化成不同的分析算法

```cpp
/// FlowTrait:
  using Data = ...;                              // Data type of Data Flow, requires operator==
  using Context = ...;                           // optional context type for transfer function
  static constexpr bool is_forward = true/false; // forward or backward analysis
  static Data boundary(Context& ctx);            // initval of entry/exit nodes, ctx is optional
  static Data top();                             // initval of other nodes (identity element of meet)
  static Data meet(const Data& a, const Data& b);
  static Data transfer(Block& blk, const Data& in, Context& ctx);  // transfer function, context is optional
  static Data edge_transfer(Block* from, Block* to, const Data& data, Context& ctx);  // edge transfer function, context is optional
  static Context init(const ControlFlowGraph& cfg);  // optional context initializer
```

我们还加入了 `edge_transfer` 用于在流图的边上传递，因为对 phi 指令进行活跃变量分析时需要考虑从具体哪个前驱传递到当前块。

通过不断求解 $"flow_out"[B] = "transfer"(B, "flow_in"[B])$ 和 $"flow_in"[B] = "meet"_(P in "flow_pred"(B)) "edge_transfer"(P, B, "flow_out"[P])$，直到所有节点的 IN/OUT 不再变化为止

---

=== 支配关系分析

基本块支配关系的 Trait 定义如下：

#grid(columns: (1fr, 1fr), gutter: 1em)[
  - 正向数据流
  - 值类型为基本块集合，使用 Set 数据结构实现　
  - entry初始值为空集，其余初始值为全集
  - meet 操作为集合交集
  
  - transfer 操作为将当前块加入集合
][
  ```cpp
  struct Dominance {
      static constexpr bool is_forward = true;
      static auto print(Block* blk) -> std::string {
          return blk->label;
      }
      using Data = Set<Block*, print>;
  
      static constexpr auto boundary = Data::empty;
      static constexpr auto top = Data::universe;
      static constexpr auto meet = Data::intersection_set;
  
      static Data transfer(Block& blk, const Data& in) {
          if (in.is_universe) return in;
          Data res = in;
          res.set.insert(&blk);
          return res;
      }
  };
  ```
]

---

=== 支配树和支配边界

#lemma[
  $u "dom" v, v "dom" w ==> u "dom" w$
]<lemma0>

#lemma[
  $u != v != w, u "dom" w, v "dom" w ==> u "dom" v or v "dom" u$
]<lemma1>

#proof[
  假设原点是 $s$，由于 $u,v "dom" w$，所以每一条 $s->w$ 路径上都会出现 $u,v$，不妨设先出现 $u$ 后出现 $v$: $s -> ... -> u -> ... -> v -> ... -> w$，若 $u,v$ 不存在支配关系，则存在路径 $s -> ... v -> ... -> w$ 不经过 $u$，与 $u "dom" w$ 矛盾
]

#theorem[
  一个节点的所有支配节点中不存在两个节点使得他们的支配节点集大小相同
]

#proof[
  考虑 $u,v "dom" w$，则 $u "dom" v$ 或 $v "dom" u$，对应 $abs("dom"(u)) < abs("dom"(v))$ 或 $>$，与 $abs("dom"(u))=abs("dom"(v))$ 矛盾
]

---

因此，可以得到求支配树的算法：对于每一个节点 $w$，遍历其支配节点集中的节点，支配集合最大的节点 $v$ 就是 $w$ 的直接支配节点，每一个节点都有唯一的直接支配节点，且由 @lemma0, @lemma1 保证直接支配关系不成环，形成支配树

```cpp
struct DominanceTree {
    DominanceTree(const DataFlow<flow::Dominance>& dom_flow) {
        auto& func = dom_flow.cfg.func;
        auto& dom = dom_flow.out;
        for (const auto& block_box : func.blocks()) {
            auto block = block_box.get();
            if (block == func.entrance())
                idom_map[block] = nullptr;  // entry block has no dominator
            Block* idom_block = nullptr;
            for (auto dom_block : dom.at(block)) {
                if (dom_block == block) continue;
                if (!idom_block || dom.at(dom_block).size() > dom.at(idom_block).size()) {
                    idom_block = dom_block;
                }
            }
            idom_map[block] = idom_block;
        }
        for (auto& [child, parent] : idom_map) if (parent) children_map[parent].push_back(child);
    }
}
```

---

#definition(title: "支配边界")[
  $"DF"(x)={y | exists p in "pred"(y), p != y and x "dom" p and not (x "dom" y)}$
]

=== 活跃变量分析

活跃变量分析是后向数据流分析。变量 $v$ 在程序点 $p$ 活跃，当且仅当存在一条从 $p$ 到某使用点的路径，且路径上 $v$ 未被重定义。

`Liveness` trait 中：
- `Data = Set<LeftValue>`，`is_forward = false`
- `top() = empty`，`meet = union_set`
- `transfer(blk, out) = (out \\ "kill"[blk]) union "gen"[blk]`
- `edge_transfer` 将 Phi 指令的操作数视为"在对应的前驱边末尾"被使用，这是 Phi 语义所要求的

活跃变量信息用于死代码消除：如果一个定值处定义的变量在被使用前又重新定值，则原定值可消除。此外，SSA 构造中 Phi 插入时也需检查变量在目标块是否活跃——若不活跃则无需插入。

#split-full

== 生成 SSA

SSA（Static Single Assignment）形式要求每个变量在静态程序文本中仅被定义一次。我们通过"插入 Phi + 重命名"两步算法实现。

=== 插入 Phi

对于每个可能被多次赋值的变量（`immutable = false` 的 `Alloc`），算法如下：
1. 收集该变量所有定值所在的基本块，设为 `in_worklist`。
2. 以 `in_worklist` 为初始工作队列，对每个块 $B$，若其支配边界中的某个块 $F$ 中该变量活跃（`live_in`），则需在 $F$ 开头插入该变量的 Phi 指令。
3. 将 $F$ 加入队列继续传播，因为 $F$ 中新插入的 Phi 本身也是一个定值。

```cpp
while (worklist.size()) {
    auto block = worklist.front(); worklist.pop();
    for (auto frontier : dom_frontier.frontier(block)) {
        if (!live_vars.in[frontier].contains(val)) continue;
        if (!has_phi[frontier]) {
            auto phi = PhiInst{.result = val};
            for (auto pred : cfg.pred[frontier])
                phi.args.emplace_back(pred, LeftValue{val});
            frontier->prepend(Inst{std::move(phi)});
            has_phi[frontier] = true;
        }
        if (!in_worklist.count(frontier)) { worklist.push(frontier); in_worklist.insert(frontier); }
    }
}
```

#split

=== 重命名变量

插入 Phi 后，通过一次支配树上的先序遍历为每个变量赋予版本号。维护一个 `rename_stack[alloc]` 栈：

1. 进入块时，遇到定值则生成新版本号并压入栈；遇到使用则替换为栈顶的 `SSAValue`。
2. Phi 指令的操作数仅在对应该前驱块的边末端进行重命名（在遍历到前驱块的后续块时处理）。
3. 递归处理支配树上的子块。
4. 退出块时，将本块压入的版本出栈。

重命名完成后，所有 `Alloc` 的 `immutable` 标记置为 `true`，因为此后不再有对同一 `Alloc` 的第二次定值。

```cpp
void rename(Block& block, const Func& func) {
    for (auto& inst : block.insts()) {
        if (!is_phi(inst))
            for (auto use : used_vars(inst)) rename_var(*use);  // 1. 重命名使用
        if (auto def = defined_var(inst); def)
            push_new_version(def);                              // 2. 重命名定值
    }
    for (auto& succ : cfg->succ[&block])
        for (auto& phi : phi_insts(succ))
            rename_phi_arg(phi, &block);                        // 3. 重命名后继的 Phi 参数
    for (auto& child : dom_tree->children(&block))
        rename(*child, func);                                   // 4. 递归子节点
    pop_pushed_versions();                                      // 5. 退出：恢复栈
}
```

#split-full

== SSA IR 的优化

我们实现了一个基于 Pass 框架的优化管线。每个 Pass 遵循统一接口 `SSAPass::apply(Program&, SSAPassContext&)`，返回 `bool` 表示是否有改变。`SSAPassContext` 持有 `UseDefInfo`，一个基于回调自动维护的定值-使用映射表，随 IR 修改实时更新。

优化 Pass 通过 `compiler.cpp` 中的 `apply` 函数迭代执行，外层 `while (apply(program, ctx, passes))` 循环保证各 Pass 反复运行至不动点。

=== 死代码消除

==== 定值

`DeadDefElimination` 扫描所有指令，若某条指令的 `result` 没有任何使用（`uses_of(def).empty()`）且无副作用（非 `CallInst`、非 `STORE`），则删除该指令。此 Pass 迭代执行，因为删除一条死定值可能使另一条定值变为死代码。

```cpp
for (auto& inst : block->insts()) {
    auto def = utils::defined_var(inst);
    if (def && ctx.ud.uses_of(*def).empty() && !utils::has_side_effect(inst))
        block->erase(it), changed = true;
}
```

#split

==== 变量

`DeadAllocElimination` 收集整个程序中所有被引用过的 `Alloc*`（通过 `NamedValue` 或 `SSAValue` 的 `def` 字段），将不再被引用的 `Alloc` 从 `Func.locals()` 或 `Program.globals()` 中移除。这通常在其他优化消除所有对某变量的引用后生效。

#split

==== 不可达基本块

`DeadBlockElimination` 以入口块为起点做可及性分析（DFS），将所有不可及基本块删除，同时精简 Phi 指令中指向不可及块的分支。此 Pass 也迭代执行——删除不可及块可能使更多块变得不可及。

#split

=== 复制传播

`CopyPropagation` 在 SSA 形式下传播"复制关系"：寻找 `MOV` 指令（`%1 = %0`），建立 `copies[%1] = %0` 映射。对 Phi 指令，若其所有操作数的"根"收敛到同一值，也视为复制。

```cpp
auto get_root = [&](auto self, Value v) -> Value {
    while (auto it = copies.find(v); it != copies.end())
        v = it->second;
    return v;
};
```

找到所有复制关系后，通过 `replace_all_uses_with(old_val, root)` 将所有对被复制值的引用替换为根值。此 Pass 内层也需要迭代（`while (propagate(...))`），因为第一次替换可能暴露新的复制关系。

#split

=== 常量传播

`ConstPropagation` 包含两个子模块：

#strong[常量折叠 `ConstexprFolder`] 对操作数均为编译期常量的指令进行静态求值。支持算术运算（`+`, `-`, `*`, `/`, `%`）、比较运算和逻辑运算，覆盖 `int`/`float`/`double`/`bool` 四种基本类型。

#strong[传播] 扫描所有指令：若一元/二元指令的操作数可折叠为常量，或 Phi 指令的所有来源均为同一常量，则将结果替换为该常量，通过 `replace_all_uses_with` 传播。此外，扫描 `BranchExit` 的条件——若条件为编译期布尔常量，直接将分支出口替换为 `JumpExit`。

```cpp
// 常量折叠分支: if (true) → jump true_target
if (auto c = std::get_if<ConstexprValue>(&branch->cond)) {
    if (c->type.is<Bool>()) {
        bool cond = std::get<bool>(c->val);
        block->setExit(JumpExit{cond ? branch->true_target : branch->false_target});
    }
}
```

常量传播后需要紧跟一次复制传播，因为常量替换可能产生新的 `MOV` 复制链。两个 Pass 交替迭代至不动点。

#split

=== 简单块替换

`SimplifyCFG` 执行两种简化：

#strong[Squash（块折叠）] 若一个基本块只包含 `JumpExit` 且无指令，则其前驱块中的跳转目标可直接替换为该块的目标，同时更新所有 Phi 指令中的块引用。若目标块只有该块一个前驱（`pred.size() == 1`），则即使当前块有指令也可以合并。

#strong[Redirect（入口重定向）] 若入口块只有一条 `JumpExit` 且目标块无 Phi 指令，则可将目标块的内容合并到入口块，并交换标签使合并后的块仍为入口。

这两个操作均需检查 Phi 冲突：若替换块的前驱在目标块的 Phi 中已存在，则拒绝合并。

#split

=== 公共子表达式消除

`CommonSubexpressionElimination`（CSE）基于支配树上的作用域值编号（scoped value numbering）算法。

#strong[编码] 将每条指令编码为 `(op, arg1_num, arg2_num)` 元组，其中 `op` 为操作符的 `uint16_t` 表示（一元指令直接取枚举值，二元指令加 `0x1000` 偏移以区分），`arg_num` 为操作数在当前作用域的值编号。

#strong[消去] 以支配树先序遍历各基本块，每遇到一条指令，若其编码在作用域的 `expr_num` 表中已存在，则将其替换为对已有值的 `MOV` 指令（后续复制传播会进一步消除此 MOV）；否则将该指令的结果值编号注册到表中。

```cpp
Expr expr{op, arg1_num, arg2_num};
if (ctx.expr_num.count(expr)) {
    changed = true;
    return UnaryInst{UnaryInstOp::MOV, inst.result, ctx.num_value[ctx.expr_num[expr]]};
} else {
    ctx.expr_num[expr] = ctx.value_num[inst.result];
    return inst;
}
```

CSE 跳过内存操作（`LOAD`、`STORE`、`LOAD_ELEM`）和 `MOV` 指令本身——`MOV` 跳过是为了避免跨变量 SSA 引用导致的共享存储错误（详见 VM 文档），也避免产生 `MOV` 链使迭代不收敛。

#split

=== 内联展开

`Inlining` 将函数调用替换为被调用函数的函数体。仅当被调用函数满足以下条件时展开：
- 非 `main` 函数
- 指令总数不超过阈值（默认 8 条）
- 无递归调用

展开流程：
1. 在调用指令处 `split` 基本块：前半段为调用前的指令，后半段（`remain`）为调用后的指令。
2. `clone` 被调用函数，为其中的临时变量在调用者处重新分配 id，并更新引用。
3. 创建 `prologue` 块：包含将实参复制到形参的 `MOV` 指令，然后跳转到被调用函数的入口。
4. 收集被调用函数的所有 `ReturnExit` 块，将其出口改写为跳转到 `epilogue` 块。
5. 创建 `epilogue` 块：包含一个 Phi 指令合并所有返回路径的值，然后跳转到 `remain`。
6. 将被调用函数的参数、局部变量和基本块全部移至调用者。

完成后函数的 `inline_{name}_{id}_` 前缀克隆体被嵌入调用者，原函数从 `Program.funcs` 中移除。