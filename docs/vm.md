# Note of Virtual Machine

View 是对于内存资源的引用，不持有所有权，包含类型 .type 和地址 .data

注意数组和指针的语义不一样：数组的.data指向的是缓冲区开始，而指针的.data指向的是一个指针，这个指针指向的才是缓冲区

---

操作数通过 `view_of` 实现从 IR 操作数 (三种值) 到 VM 操作数 View 的转换，由于 view 不持有内存，进入函数时就需要分配好所有的局部变量和临时变量

---

实参由被调用者处理，复制到被调用者的栈帧中，因此栈帧包含三部分：自己的实参，局部变量，临时变量。

---

对于一个 `Func` 类型的 View，它的 `data` 指向的是一块大小为 `sizeof(Func*)` 的内存，值为 `Program` 中对应 `Func` 对象的地址，这样在执行 CallInst 的时候就能通过操作数的 data 获取 Func，然后调用 `execute(func_ptr, args, ret)`
