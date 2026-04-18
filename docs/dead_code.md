# 死代码消除

## 简单块消除

```
TrivialBlockElimination failed: compiler error : conflict phi args, caused by replacng if_true_8_4 to entry in $a_0.4: i32 = $phi('entry: 5, 'if_true_8_4: 25); (at /Users/ycp/Source/Courses/cp-lab/main/src/backend/ir/optim/dead_block.hpp:124)
Current program: fn if_if_Else() -> i32 {
    let a_0: i32;
    'entry: {
        jump 'if_exit_8_4;
    }
    'if_exit_7_2: {
        $a_0.3: i32 = $phi('if_exit_8_4: $a_0.4);
        return $a_0.3;
    }
    'if_true_8_4: {
        jump 'if_exit_8_4;
    }
    'if_exit_8_4: {
        $a_0.4: i32 = $phi('entry: 5, 'if_true_8_4: 25);
        jump 'if_exit_7_2;
    }
}

fn main() -> i32 {
    'entry: {
        $0: i32 = if_if_Else();
        return $0;
    }
}
```

不能直接把所有的只有jump的块合并，可能会导致phi冲突

```cpp
    bool conflicts(Block* replaced, Block* target, ControlFlowGraph& cfg) {
        // Check: would any predecessor of `replaced` conflict with
        // an existing phi source in `target`?
        for (auto& inst : target->insts()) {
            if (auto phi = std::get_if<PhiInst>(&inst); phi) {
                for (auto pred : cfg.pred[replaced]) {
                    if (phi->args.count(pred)) return true;
                }
            } else {
                break;
            }
        }
        return false;
    }

```