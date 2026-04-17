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
