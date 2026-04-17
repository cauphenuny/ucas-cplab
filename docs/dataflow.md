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

#### SSA Phi 导致的活跃变量污染问题 (Bug Fix)

在引入 SSA 形式后，最初的活跃变量分析实现中存在一个严重 bug：$\phi$ 指令的操作数（Uses）被错误地视为在包含该指令的基本块（Block）入口处统一使用。

**影响**：
- 活跃变量会被错误地传播到所有前驱块。
- 甚至会导致函数定义中不存在的变量被认为在 `entry` 块入口处活跃。

**解决方案**：
1. **引入边转移 (Edge Transfer)**：在 `DataFlow` 求解器框架中增加了对 `edge_transfer` 的支持。对于逆向分析（如活跃变量），交汇运算（Meet）时会先针对每条边调用 `edge_transfer`。
2. **特定的 Phi 处理**：在 `Liveness` 分析类中，将 $\phi$ 指令的操作数从通用的 `gen` 集合中移除，改为记录在 `phi_uses[dst_block][src_block]` 中。
3. **按边激活**：实现 `edge_transfer(src, dst, data)`，当数据流从 `dst` 回传至 `src` 时，仅将对应于 `src` 分支的 $\phi$ 操作数加入到活跃变量集中。
