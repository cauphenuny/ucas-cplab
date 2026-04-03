# Code of Compiler Lab of UCAS, 2026

Build:

```
$ cd grammar
$ java -jar ../deps/antlr-4.13.1-complete.jar -Dlanguage=Cpp CACT.g4 -visitor -no-listener
$ cd ..
$ cmake -Bbuild
$ cmake --build build
```

Usage:

```
build/compiler [--ast] [--sem] [--ir] [--exec] [--silent] files ... [--output <output file>] [--help]
    --ast       Print the AST of the input files
    --sem       Print the semantic analysis result of the input files
    --ir        Print the generated IR of the input files
    --exec      Execute the generated IR in virtual machine
    --silent    Suppress all compiler output except the return value when executing
    --output    Write the generated IR also to the specified file
    --help      Show this help message
```

Examples:

- print IR: `build/compiler --ir source.cact`
- output IR to file: `build/compiler --ir source.cact --output ir_code.rs`
- simulate program: `build/compiler --exec source.cact`
- simulate program, sliently (without any output except program IO and return code): `build/compiler --exec --silent source.cact`

---

Design Notes:

- [IR](docs/ir.md)

- [IR Virtual Machine](docs/vm.md)

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
    Pointer(Box<Type>),
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
    Named(Type, ASTNode),
    Temp(Type, usize),
    Const(Type, ConstExp),
}
enum Inst {
    Unary(UnaryOp, Value, Value),
    Binary(BinaryOp, Value, Value, Value),
    Call(Value, Value, Vec<Value>),
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