# Note of Intermediate Representation

type.hpp: 支持子类型判断、constructable 判断的 ADT

---

三种值：NamedValue/TempValue/ConstexprValue

分别对应变量，临时值和编译期常量

ConstexprValue 由于需要持有内存（不然 initializer_list 的内存由谁管理？），不能拷贝，只能移动。而这个不能拷贝的特性正好阻止了编译期常量同时作为一个指令的结果操作数和另一个指令的输入操作数。

对于多维初始化数组，为了让 VM 好写，ConstexprValue维护的是一整块连续的内存。

TempValue 的编号作用域是函数，因此 VM 中可以和局部变量一起预先分配

---

函数由基本块 `Block` 和局部变量 `Alloc` 构成，每个基本块有且只有一个出口，分别是 jump, branch或者return.

---

生成 IR 时，维护一个 `Func* func` 用于产生新的基本块，维护一个 `Block* scope` 表示当前在这个基本块中产生指令

对于 expression，返回值是 `Value` \
对于 instrument，返回值是 `Block*` 表示当前指令执行完之后，会进入哪个基本块，之后在这个基本块添加指令，若返回的是 `nullptr`，则当前指令一定会 `return`，不存在下一个执行的指令

if-stmt / while-stmt / branch (为了短路) 在需要时产生新的基本块，具体见 `ir/gen/stmt.cpp`
