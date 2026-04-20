# Code of Compiler Lab of UCAS, 2026

Build:

```
$ cmake -Bbuild -DCMAKE_BUILD_TYPE=Release
$ cmake --build build
```

ANTLR generation is integrated into CMake. As long as Java is available and
`deps/antlr-4.13.1-complete.jar` exists, parser sources are generated automatically
during the build.

Usage:

<!--usage-->
```
compiler [args]... files ...

    --help                  Show this help message

    --ast                   Print the AST of the input files
    --ast-info              Print the semantic analysis result of the AST

    --ir                    Print the generated IR
    --ir-info               Print analysis result of the generated IR
    --ssa                   Convert generated IR to SSA form
    --ssa2temp              Convert SSAValue in IR to TempValue

    --optimize-copy         Apply Copy Propagation optimization (triggers --ssa)
    --optimize-const        Apply Const Propagation optimization (triggers --ssa)
    --optimize-def          Apply Dead Definition Elimination optimization (triggers --ssa)
    --optimize-alloc        Apply Dead Allocation Elimination optimization (triggers --ssa, better with --ssa2temp)
    --optimize-block        Apply Dead/Trivial Block Elimination optimization (triggers --ssa)
    --optimize-inline [N=8] Apply Function Call Inlining optimization (threshold: N insts) (triggers --ssa)
    --optimize-exp          Apply Common Subexpression Elimination optimization (triggers --ssa)
    -O1, --optimize         Apply above optimizations, --no-optimize-[...] to disable specific optimizations

    --exec                  Execute the generated IR
    --silent                Suppress all compiler output except the return value when executing

    --output <file>         Write the generated IR also to the specified file

interpreter [--help] [--silent] [--print] IR_file
    --help      Show this help message
    --print     Show reconstructed IR, without executing
    --silent    Suppress all compiler output except the return value when executing
```
<!--/usage-->

Examples:

- print IR: `build/compiler --ir source.cact`
- print optimized IR: `build/compiler --ir --optimize source.cact`
- output IR to file: `build/compiler --ir source.cact --output ir_code.riir`
- execute source program: `build/compiler --exec source.cact`
- execute source program, sliently (without any output except program IO and return code): `build/compiler --exec --silent source.cact`
- execute IR program: `build/interpreter ir_code.riir`
- execute IR program, silently: `build/interpreter ir_code.riir --silent`

---

Design Notes:

- [Our IR: RIIR](docs/ir.md)

- [Virtual Machine of IR](docs/vm.md)

Pipeline:

```
source -> IR(RIIR) -> target(rv64)
```

---

<!--source_tree-->
```
src/
├── backend/
│   ├── ir/
│   │   ├── alloc.cpp
│   │   ├── analysis/
│   │   │   ├── cfg.hpp:	Control Flow Graph
│   │   │   ├── dataflow/
│   │   │   │   ├── dominance.hpp
│   │   │   │   ├── framework.hpp:	Unified Data Flow Equation Solver
│   │   │   │   └── liveness.hpp:	Live Variable Analysis
│   │   │   ├── dominance.hpp
│   │   │   └── utils.hpp
│   │   ├── block.cpp
│   │   ├── func.cpp
│   │   ├── gen/
│   │   │   ├── decl.cpp
│   │   │   ├── expr.cpp
│   │   │   ├── irgen.h
│   │   │   └── stmt.cpp
│   │   ├── inst.cpp
│   │   ├── ir.h
│   │   ├── op.hpp
│   │   ├── optim/
│   │   │   ├── common_expr.hpp:	Common Subexpressions Elimination, requires SSA
│   │   │   ├── const_propagation.hpp:	Const Propagation Pass, requires SSA
│   │   │   ├── copy_propagation.hpp:	Copy Propagation Pass, requires SSA
│   │   │   ├── dead_alloc.hpp:	Dead Allocation Elimination Pass
│   │   │   ├── dead_block.hpp:	CFG Simplification & Dead Block Elimination Pass, requires SSA
│   │   │   ├── dead_def.hpp:	Dead Definition Elimination Pass, requires SSA
│   │   │   ├── framework.hpp
│   │   │   ├── inline.hpp:	Inline Pass, requires SSA
│   │   │   └── ssa.hpp:	SSA Construct Pass
│   │   ├── parse/
│   │   │   └── visit.hpp
│   │   ├── program.cpp
│   │   ├── type.hpp:	algebraic data types for IR
│   │   ├── value.cpp
│   │   └── vm/
│   │       ├── assign.cpp
│   │       ├── exec.cpp
│   │       ├── view.hpp
│   │       └── vm.h
│   └── rv64/
│       └── inst.hpp
├── compiler.cpp
├── frontend/
│   ├── ast/
│   │   ├── analysis/
│   │   │   ├── decl.cpp
│   │   │   ├── expr.cpp
│   │   │   ├── func.cpp
│   │   │   ├── scope.cpp
│   │   │   ├── semantic_ast.h
│   │   │   ├── stmt.cpp
│   │   │   └── type.cpp
│   │   ├── ast.hpp
│   │   └── op.hpp
│   └── syntax/
│       ├── error.hpp
│       └── visit.hpp
├── interpreter.cpp
├── tests/
│   ├── test_adt.cpp
│   ├── test_ast.cpp
│   ├── test_dominance.cpp
│   ├── test_ir_parse.cpp
│   ├── test_ir_parse_all.cpp
│   ├── test_liveness.cpp
│   ├── test_livenesss_all.cpp
│   ├── test_optimize.cpp
│   ├── test_sem.cpp
│   ├── test_serialize.cpp
│   ├── test_ssa.cpp
│   └── test_vm.cpp
└── utils/
    ├── diagnosis.hpp
    ├── match.hpp
    ├── serialize.hpp
    ├── traits.hpp
    └── tui.h

15 directories, 64 files
```
<!--/source_tree-->

---

IR Type System:

```rust
enum PrimitiveType {
    Int,
    Float,
    Double,
    Bool,
}
enum Type {
    Top,
    Bottom,
    Sum(Vec<Type>),
    Product(Vec<Type>),
    Func(Product, Box<Type>),
    Array(Box<Type>, usize),
    Reference(Box<Type>, bool, bool),
    Primitive(PrimitiveType),
}
```

---

AST:
```rust
enum ConstExp {
    Int(i32),
    Float(f32),
    Double(f64),
    Bool(bool),
}
enum LValExp {
    Name(String),
    WithIndex(Exp),
}
enum PrimaryExp {
    Box(Box<Exp>),
    LVal(LValExp),
    Const(ConstExp),
}
enum Exp {
    Const(ConstExp),
    Primary(PrimaryExp),
    Unary(UnaryOp, Box<Exp>),
    Binary(BinaryOp, Box<Exp>, Box<Exp>),
    Call(LVal, Vec<<Exp>),
}
enum Stmt {
    Exp(Exp),
    Assign(LValExp, Exp),
    If(Exp, Box<Stmt>),
    While(Exp, Box<Stmt>),
    Return(Option<Exp>),
    Break,
    Continue,
    Block(Vec<BlockItem>),
}
enum Decl {
    Const(ConstDecl),
    Var(VarDecl),
}
enum BlockItem {
    Decl(Decl),
    Stmt(Stmt),
}
enum ConstInitVal {
    Exp(ConstExp),
    Array(Vec<ConstInitVal>),
}
struct ConstDef(String, Vec<Option<usize>>, ConstInitVal)
struct VarDef(String, Vec<Option<usize>>, Option<ConstInitVal>)
struct ConstDecl(Type, Vec<ConstDef>)
struct VarDecl(Type, Vec<VarDef>)
struct FuncParam(Type, String, Vec<Option<usize>>)
struct FuncDef(Type, String, Vec<FuncParam>, Stmt)
enum CompUnitItem {
    Decl(Decl),
    Func(FuncDef),
}
struct CompUnit(Vec<CompUnitItem>)
```

---

IR:

```rust
enum Value {
    Named(Type, &Alloc),
    Temp(Type, usize),
    Const(Type, ConstExp),
}
enum Inst {
    Unary(UnaryOp, Value, Value),
    Binary(BinaryOp, Value, Value, Value),
    Call(Value, Value, Vec<Value>),
    Phi(Value, Vec<&Block, Value>)
}
enum Exit {
    Return(Value),
    Branch(Value, &Block, &Block),
    Jump(&Block),
}
struct Block(String, Vec<Inst>, Exit)
struct Alloc(Value, Option<Value>)
struct Func(Type, String, Vec<(Type, String)>, Vec<Alloc>, Vec<Block>)
struct Program(Vec<Alloc>, Vec<Func>)
```

---

[Tutorial](assets/tutorial.md)