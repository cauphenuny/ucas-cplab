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

== 生成SSA

=== 插入 phi

=== 重命名变量

== SSA IR 的优化

以下大致按实现先后顺序排列

=== 死代码消除

==== 定值

==== 变量

==== 不可达基本块

=== 复制传播

=== 常量传播

=== 简单块替换

=== 公共子表达式消除

=== 内联展开