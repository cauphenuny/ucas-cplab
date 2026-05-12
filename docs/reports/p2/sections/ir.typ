#import "../../preamble/preamble.typ": *

== IR 结构

根据前问所述 IR 设计，我们 IR 的数据结构如下：

```cpp
using NameDef = std::variant<const Alloc*, const Func*, const BuiltinFunc*>;

struct NamedValue { Type type; NameDef def; };
struct TempValue { Type type; size_t id; };
struct ConstexprValue { Type type; std::variant<std::monostate, int, float, bool, double, std::unique_ptr<std::byte[]>> val; };
struct SSAValue { Type type; const Alloc* def; size_t version; };

using Value = std::variant<NamedValue, TempValue, ConstexprValue, SSAValue>;
using LeftValue = std::variant<NamedValue, TempValue, SSAValue>;

struct UnaryInst { LeftValue result; UnaryInstOp op; Value operand; };
struct BinaryInst { LeftValue result; InstOp op; Value lhs, rhs; };
struct CallInst { LeftValue result; NamedValue func; std::vector<Value> args; };
struct PhiInst { LeftValue result; std::vector<std::pair<Block* Value>> args; };
using Inst = std::variant<UnaryInst, BinaryInst, CallInst, PhiInst>;

struct ReturnExit { Value; exp; };
struct BranchExit { Value cond; Block *true_target, *false_target; };
struct JumpExit { Block* target; };
using Exit = std::variant<ReturnExit, BranchExit, JumpExit>;

struct Block { std::string label; std::list<Inst> insts; std::optional<Exit> exit; };
struct Alloc { std::string name; Type type; bool comptime, immutable, reference; };
struct Func {
    std::string name;
    Type ret_type;
    std::vector<std::unique_ptr<Alloc>> params;
    std::vector<std::unique_ptr<Alloc>> locals;
    std::list<std::unique_ptr<Block>> blocks;
    struct TempInfo {
        Type Type;
        Block* block;
    };
    std::vector<TempInfo> temps;
    struct LoopContext {
        Block* continue_target;
        Block* break_target;
    };
    std::vector<LoopContext> loops;  // for break/continue target
};
struct BuiltinFunc {
    std::string name;
    Type type;
};
struct Program {
    std::vector<std::unique_ptr<Alloc>> globals;
    std::vector<std::unique_ptr<Func>> funcs;
    std::vector<std::unique_ptr<BuiltinFunc>> builtins;
};
```

---

== IR 生成

类似类型检查，我们的 IR 生成也是通过从上到下遍历一遍 AST 得到的，

```cpp
struct Generator {
    std::unique_ptr<Program> generate(const ast::SemanticAST& info);

private:
    const ast::SemanticAST* info;
    const ast::CompUnit* ast;
    std::unordered_map<ast::SymDefNode, ir::NameDef> ir_defs;

    std::unordered_map<std::string, size_t> var_name_count;
    std::unordered_map<ast::VarDefNode, std::string> var_names;
    auto name_of(ast::SymDefNode def) -> std::string;

    auto gen(const ast::ConstInitVal* init, Type target_type) -> ConstexprValue;
    auto gen(const ast::VarDef* def) -> std::unique_ptr<Alloc>;
    auto gen(const ast::ConstDef* def) -> std::unique_ptr<Alloc>;
    auto gen(const ast::FuncParam* param) -> std::unique_ptr<Alloc>;
    auto gen(const ast::Decl* decl) -> std::vector<std::unique_ptr<Alloc>>;

    auto gen(const ast::FuncDef* func) -> std::unique_ptr<Func>;

    auto gen(const ast::BlockStmt* block_stmt, Func* func, Block* scope) -> Block*;
    auto gen(const ast::Stmt* stmt, Func* func, Block* scope) -> Block*;
    auto gen(const ast::StmtBox* stmt_box, Func* func, Block* scope) -> Block*;

    auto gen(const ast::LVal* lval) -> NamedValue;
    auto gen(const ast::LValExp* exp, Func* func, Block* scope) -> LeftValue;
    auto gen(const ast::BinaryExp* exp, Func* func, Block* scope) -> Value;
    auto gen(const ast::ConstExp* exp, Func* func, Block* scope) -> Value;
    auto gen(const ast::Exp* exp, Func* func, Block* scope) -> Value;
    auto gen(const ast::ExpBox* exp_box, Func* func, Block* scope) -> Value;

    auto branch(const ast::Exp* cond, Func* func, Block* scope, Block* true_block,
                              Block* false_block) -> BranchExit;
};
```

---

- 对于变量定义和函数形式参数定义，我们返回对应类型的 `Alloc`

- 对于函数定义，我们返回一个 `Func`

- 对于语句，我们返回一个 `Block*`，表示接下来应该在这个 block 中 emit IR，如果为空，表示当前语句已经 return，离开函数作用域了

- 对于表达式，我们返回一个 `Value`，表示这个表达式的结果值，计算这个表达式的指令会被 emit 到当前 block 中 （每一个表达式都会分配一个 Value）

以上所有可能产生 IR 的位置都会有继承属性 `func, scope`，表示当前所在的函数和 block 作用域，分别用于分配临时值和产生IR。

- 语句返回 `Block*` 主要是用于产生新基本块的场景，比如 if 语句产生 `then_block, else_block, exit_block`，在生成两个分支时，scope 属性会设置为对应的 then/else block，同时gen函数返回 `exit_block` 以供后续语句继续 emit IR

- 对于嵌套多级表达式，因为我们对于每一个表达式都返回的是 Value，因此每一级表达式只需要分配少量新临时值，根据自己的运算符以及操作数表达式返回的 Value，生成少量 IR 指令。

== IR 解释执行

=== 核心数据结构: View

View 是对于 IR 中值的运行时表示，通过类型信息支持运行时多态，使用一个指针指向缓冲区，不持有缓冲区内存所有权。

```cpp
struct View {
    std::byte* data;
    ir::Type type;
};
```

=== 对 View 的赋值与运算

这是虚拟机的核心部分，IR 中的每一个指令都对应着对于 View 的某种操作，IR 解释器通过模式匹配 IR 指令来执行对应的 View 操作。

---

==== 赋值

```cpp
    template <typename T1, typename T2>
    void assign(const T1& dest_type, std::byte* dest, const T2& src_type,
                const std::byte* src) const {
        throw COMPILER_ERROR(fmt::format("Cannot assign {} to {}", src_type, dest_type));
    }

    void assign(const ir::type::Reference& dest_type, std::byte* dest,
                const ir::type::Primitive& src_type, const std::byte* src) const;
    void assign(const ir::type::Primitive& dest_type, std::byte* dest,
                const ir::type::Primitive& src_type, const std::byte* src) const;
    void assign(const ir::type::Sum& dest_type, std::byte* dest, const ir::Type& src_type,
                const std::byte* src) const;
    void assign(const ir::type::Array& dest_type, std::byte* dest, const ir::type::Array& src_type,
                const std::byte* src) const;
    void assign(const ir::type::Reference& dest_type, std::byte* dest,
                const ir::type::Reference& src_type, const std::byte* src) const;
    void assign(const ir::type::Reference& dest_type, std::byte* dest,
                const ir::type::Array& src_type, const std::byte* src) const;
    void assign(const ir::type::Product& dest_type, std::byte* dest,
                const ir::type::Product& src_type, const std::byte* src) const;
    void assign(const Type& dest_type, std::byte* dest, const Type& src_type,
                const std::byte* src) const;

    void assign(View& dest, const View& src) const;
```

具体实现比较简单，需要注意的只有 Sum Type 的 tag维护，以及从数组/基础类型assign到指针时的取地址处理。

---

==== 运算

核心函数：

```cpp
template <template <typename> class Op>
void eval_binary(View& dest, const View& lhs, const View& rhs) const {
    match(dest.type.as<ir::type::Primitive>(), [&](auto value) {
        using type = typename decltype(value)::type;
        if constexpr (std::is_floating_point_v<type> &&
                        std::is_same_v<Op<type>, std::modulus<type>>) {
            *(type*)dest.data = std::fmod(*(type*)lhs.data, *(type*)rhs.data);
        } else {
            *(type*)dest.data = Op<type>{}(*(type*)lhs.data, *(type*)rhs.data);
        }
    });
}

template <template <typename> class Op> void eval_unary(View& dest, const View& operand) const {
    match(dest.type.as<ir::type::Primitive>(), [&](auto value) {
        using type = typename decltype(value)::type;
        *(type*)dest.data = Op<type>{}(*(type*)operand.data);
    });
}

template <template <typename> class Op>
void eval_comparison(View& dest, const View& lhs, const View& rhs) const {
    match(lhs.type.as<ir::type::Primitive>(), [&](auto value) {
        using type = typename decltype(value)::type;
        *(bool*)dest.data = Op<type>{}(*(type*)lhs.data, *(type*)rhs.data);
    });
}
```

---

通过传入不同的 Op 模版参数实例化成不同的运算

```cpp
binary_ops[InstOp::ADD] = [this](View& dest, const View& lhs, const View& rhs) {
    eval_binary<std::plus>(dest, lhs, rhs);
};
binary_ops[InstOp::SUB] = [this](View& dest, const View& lhs, const View& rhs) {
    eval_binary<std::minus>(dest, lhs, rhs);
};
binary_ops[InstOp::MUL] = [this](View& dest, const View& lhs, const View& rhs) {
    eval_binary<std::multiplies>(dest, lhs, rhs);
};
binary_ops[InstOp::DIV] = [this](View& dest, const View& lhs, const View& rhs) {
    eval_binary<std::divides>(dest, lhs, rhs);
};

// ...
```

---

对于 `LOAD/STORE/BORROW` 指令，实现通过获取指针地址和类型信息后 assign 实现

```cpp
unary_ops[UnaryInstOp::LOAD] = [this, check_ref](View& dest, const View& operand) {
    check_ref(operand.type, false, "load");
    auto ref_type = operand.type.as<type::Reference>();
    auto addr = *(std::byte**)operand.data;
    assign(dest.type, dest.data, ref_type.elem, addr);
};
unary_ops[UnaryInstOp::BORROW] = [this](View& dest, const View& operand) {
    auto addr = operand.data;
    assign(dest.type, dest.data, ir::type::Reference::pointer(operand.type, false),
            (std::byte*)&addr);
};
binary_ops[InstOp::STORE] = [this, check_ref](View& dest, const View& lhs,
                                                const View& rhs) {
    check_ref(lhs.type, false, "store");
    auto ref_type = lhs.type.as<type::Reference>();
    auto addr = *(std::byte**)lhs.data;
    assign(ref_type.elem, addr, rhs.type, rhs.data);
};
```

对于 `LOAD_ELEM/STORE_ELEM/BORROW_ELEM`，计算出元素地址后同样通过 assign 实现

---

=== 内存分配与栈帧

内存通过进入Program或者Func时 RAII 分配，离开时自动释放

```cpp
uint8_t VirtualMachine::execute(const Program& program) {
    // ...
    size_t global_size = 0;
    /// global variables
    for (const auto& global : program.globals_) {
        global_size += padding_to(stackSize(global), alignof(std::max_align_t));
    }
    /// return value
    global_size += sizeof(int);
    auto buffer = make_aligned_unique<std::byte>(global_size, alignof(std::max_align_t));
    memset(buffer.get(), 0, global_size);
    // ...
}
```

---

函数栈帧由三部分组成：局部变量、临时值、参数

```cpp
void VirtualMachine::execute(const Func& func, const std::vector<View>& args, View& ret) {
    // ...
    size_t stack_size = 0;
    for (const auto& local : func.locals()) {
        stack_size += padding_to(stackSize(local), alignof(std::max_align_t));
    }
    for (const auto& temp : func.temps()) {
        stack_size += padding_to(stackSize(temp.type), alignof(std::max_align_t));
    }
    for (const auto& param : func.params) {
        stack_size += padding_to(stackSize(param), alignof(std::max_align_t));
    }
    auto buffer = make_aligned_unique<std::byte>(stack_size, alignof(std::max_align_t));
    memset(buffer.get(), 0, stack_size);
    // ...
}
```

当一个 `Alloc` 被标记为 `reference` 时，VM 为该变量分配的缓冲区末尾额外添加一个指针槽（因为需要通过这个指针来访问数据）（`sizeof(std::byte*)`）。 \
缓冲区布局示意：
`[ actual data (size_of(type)) ][ pointer (sizeof(std::byte*)) ]`

出于实现方便考虑，使用 `std::max_align_t` 来对齐所有变量

---

函数的核心执行循环：

```cpp
void VirtualMachine::execute(const Func& func, const std::vector<View>& args, View& ret) {
    // ...
    Block *cur_block = func.entrance(), *prev = nullptr;
    while (cur_block) {
        auto next = execute(*cur_block, prev, frame, ret);
        prev = cur_block;
        cur_block = next;
    }
}
```

基本块执行循环：遍历每一个指令，将 Value 转换为 View，然后对指令eval，最后根据 Exit 类型返回下一个基本块地址或者空指针

---

对于 call 指令，通过保存的地址来调用对应函数

```cpp
void VirtualMachine::execute(const CallInst& inst, const std::vector<View>& srcs, View& ret) {
    auto func = inst.func;
    auto def = func.def;
    match(
        def, [&](const ir::Func* func) { return execute(*func, srcs, ret); },
        [&](const ir::BuiltinFunc* builtin_func) {
            auto vm_func = BUILTIN_FUNCS.find(builtin_func->name);
            if (vm_func == BUILTIN_FUNCS.end()) {
                throw COMPILER_ERROR(
                    fmt::format("Builtin function '{}' not found", builtin_func->name));
            }
            return execute(vm_func->second, srcs, ret);
        },
        [&](const ir::Alloc*) {
            throw COMPILER_ERROR(fmt::format("Cannot call variable {}", func));
        });
}
```

---

=== builtin函数的实现

```cpp
struct /*ir::vm::*/BuiltinFunc { // not ir::BuiltinFunc
    std::function<void(View& ret, const std::vector<View>& args, std::istream& input,
                       std::ostream& output)>
        apply;
    BuiltinFunc(std::function<void(View& ret, const std::vector<View>& args, std::istream& input,
                                   std::ostream& output)>
                    apply)
        : apply(std::move(apply)) {}
};
```

每一个builtin对应上述数据结构，提供 apply 函数实现功能，apply函数接受若干个 View参数和输入输出流参数，返回一个 View。

通过输入输出流的抽象，我们可以方便地实现自动单元测试，验证虚拟机的正确性，只需要使用字符串流即可。