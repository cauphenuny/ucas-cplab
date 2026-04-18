# 数据流分析

## 框架

设计了通用的数据流求解器，理论参考龙书9.3.3 通用框架的迭代算法

要求一种数据流要求：
- 有一个数据流方向 forward/backward
- 有一个数据类型 Data
- 可选的上下文 Context 和 初始化上下文的函数 init
- 有一个边界值 boundary (用于入口)
- 有一个初始值 top (用于非入口节点的初始值)
- 有一个交汇运算 meet
- 有一个传递函数 transfer，接受一个 block 和一个 flow_in 数据，返回一个 flow_out 数据，如果有context，参数也包含context（注意这里是 flow_in 而不是 in，in 表示基本块入口的数据，out 表示基本块出口的数据，而 flow_in 根据数据流方向可以是 in 或者 out）

满足要求的数据流可以在控制流图上求解

## 几种数据流

### 基本块支配关系

```cpp
struct Dominance {
    static constexpr bool is_forward = true;
    static auto print(const Block* blk) -> std::string {
        return blk->label;
    }
    using Data = Set<const Block*, print>;

    static constexpr auto boundary = Data::empty;
    static constexpr auto top = Data::universe;
    static constexpr auto meet = Data::intersection_set;

    static Data transfer(const Block& blk, const Data& in) {
        if (in.is_universe) return in;
        Data res = in;
        res.set.insert(&blk);
        return res;
    }
};
```

### 活跃变量

逆向数据流分析

通过 context 保存预处理的全局变量集合和每个 block 的 gen/kill

注意 boundary 是全局变量集合，不是空集

#### SSA Phi 导致的活跃变量泄漏问题 (Bug Fix)

在引入 SSA 形式后，活跃变量分析需要特殊处理 $\phi$ 指令。最初的实现中存在一个严重 bug：在计算基本块的 `gen` 集合（即在该块中先于定义而被使用的变量）时，错误地包含了 $\phi$ 指令的操作数。

**Bug 表现**：
- **活跃变量泄漏**：在 `while` 循环等结构中，循环内部定义的变量版本（如 `$i.2`）会被传播到 `entry` 块，导致测试报告 `[FAIL] live-in at entry`。
- **原因分析**：在标准的活跃分析中，`gen` 集合中的变量被视为在基本块入口处活跃。但 SSA 的 $\phi$ 操作数实际上是“来自于特定前驱块”的使用。如果将其放入 `gen` 集合，该变量就会对所有前驱块都变为活跃，这违反了 SSA 语义。

**解决方案**：
1. **使用边转移 (Edge Transfer)**：在 `DataFlow` 框架中支持 `edge_transfer` 机制。对于 $\phi$ 指令 $x = \phi(B_1: v_1, B_2: v_2)$，变量 $v_1$ 仅在从 $B_1$ 进入该块时才是活跃的。
2. **特定的 Phi 处理**：
   - 在 `Liveness::init` 中，遍历指令计算 `gen` 集合时，**跳过** $\phi$ 指令的操作数。
   - 将 $\phi$ 的操作数映射记录在 `phi_uses[current_block][predecessor_block]` 中。
3. **按边应用**：在 `edge_transfer(src, dst, data)` 中，根据具体的边 $(src \to dst)$，将对应的 $v_i$ 加入到传回给 $src$ 的活跃变量集中。

通过这一改动，$\phi$ 操作数被正确地约束在特定的路径上，解决了非法泄漏到 `entry` 块的问题。
