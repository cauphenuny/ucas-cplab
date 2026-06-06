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