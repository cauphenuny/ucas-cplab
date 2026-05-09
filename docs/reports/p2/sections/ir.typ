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

== IR 生成

== IR 解释执行
