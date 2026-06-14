=== rv64 汇编数据结构

IR 降级和寄存器分配完成后，每条 IR 指令的操作数都绑定到了具体的物理寄存器或栈位置。我们需要将 IR 指令翻译为 RISC-V 汇编代码。首先定义汇编层面的数据结构。

==== 寄存器

RISC-V 有 32 个整数寄存器和 32 个浮点寄存器。取一个 `uint8_t` 存编号，`toString()` 返回 ABI 名称：

```cpp
struct GeneralReg {
    uint8_t id = 0;  // x0 - x31
    std::string toString() const;  // 如 "zero", "ra", "sp", "t0", "a0"...
};

struct FloatReg {
    uint8_t id = 0;  // f0 - f31
    std::string toString() const;  // 如 "ft0", "fa0", "fs0"...
};
```

`toString()` 内部用一个 `static std::array<const char*, 32>` 查表，只初始化一次。

==== 真实指令

RISC-V 指令按操作数格式分六种。各定义一个 `struct`，字段对应操作数：

R 型——三个寄存器：

```cpp
struct InstR  { OpR op; GeneralReg rd, rs1, rs2; };
// add t0, t1, t2

struct InstFR { OpFR op; FloatReg rd, rs1, rs2; };
// fadd.s fa0, ft0, ft1
```

I 型——两个寄存器加一个 12 位有符号立即数。Load/Store 也走 I 型格式，`toString()` 根据 op 自动选输出格式：

```cpp
struct InstI  { OpI op; GeneralReg rd, rs1; int32_t imm; };
// addi t0, t1, 42     (算术)
// ld   t0, 8(sp)      (Load)
// sd   t0, 8(sp)      (Store)

struct InstFI { OpFI op; FloatReg rd; GeneralReg rs1; int32_t imm; };
// fld fa0, 0(t0)      (浮点 Load/Store 用整数寄存器做基址)
```

J 型——无条件跳转，目标用字符串标签：

```cpp
struct InstJ  { OpJ op; GeneralReg rd; std::string target; };
// jal ra, .Lmain_entry
```

U 型——加载高位立即数：

```cpp
struct InstU  { OpU op; GeneralReg rd; int64_t imm; };
// lui t0, 0x12345
```

==== 伪指令

有些操作 RISC-V 没有对应机器指令，但汇编器支持伪指令。我们直接用伪指令，由汇编器展开。好处是指令选择阶段不用手动做展开。

两寄存器伪指令 (`PseudoR`)，隐含第三操作数为 `zero` 或 `-1`：

```cpp
struct PseudoR {
    enum Op : uint8_t {
        MV,      // mv rd, rs  → addi rd, rs, 0
        NOT,     // not rd, rs → xori rd, rs, -1
        NEG,     // neg rd, rs → sub rd, zero, rs
        NEGW,    // negw rd, rs → subw rd, zero, rs
        SEQZ,    // seqz rd, rs → sltiu rd, rs, 1
        SNEZ,    // snez rd, rs → sltu rd, zero, rs
    } op;
    GeneralReg rd, rs1;
};
```

加载任意 32 位立即数 (`PseudoLI`)：

```cpp
struct PseudoLI { GeneralReg rd; int64_t imm; };
// 小立即数: li t0, 42    → addi t0, zero, 42
// 大立即数: li t0, 0xABCDE800
//            → lui  t0, hi20
//              addi t0, t0, lo12
```

大立即数的拆解需要考虑 ADDI 的符号扩展：若 `lo12` 的最高位是 1，`LUI` 的 `hi20` 要加 1，`lo12` 要减 0x1000，否则最终值会偏 0x1000。

分支伪指令 (`PseudoB`)——RISC-V 没有 `beqz`/`bnez`，但汇编器认识：

```cpp
struct PseudoB {
    enum Op : uint8_t { BEQZ, BNEZ };
    GeneralReg rs1;
    std::string target;
};
// beqz t0, .L_exit → beq t0, zero, .L_exit
```

跳转和调用伪指令 (`PseudoJ`)：

```cpp
struct PseudoJ {
    enum Op : uint8_t { J, CALL };
    std::string target;
};
// j label     → jal zero, label
// call func   → auipc ra, ...; jalr ra, ...
```

全局变量访问伪指令 (`PseudoL`)——访问全局变量需要先取地址再读写，合并为一条伪指令。内部会占用 `t0`(x5) 做临时寄存器：

```cpp
struct PseudoL {
    enum Op : uint8_t {
        LA,       // la rd, symbol  → auipc rd, ...; addi rd, ...
        LGD,      // 加载 64 位: la t0, symbol; ld rd, 0(t0)
        LGW,      // 加载 32 位: la t0, symbol; lw rd, 0(t0)
        SGD,      // 存储 64 位: la t0, symbol; sd rd, 0(t0)
        SGW,      // 存储 32 位: la t0, symbol; sw rd, 0(t0)
    } op;
    GeneralReg rd;
    std::string symbol;
};
```

返回伪指令 (`PseudoRet`)，展开为 `jalr zero, ra, 0`。

所有指令类型用 `std::variant` 统一：

```cpp
using Inst = std::variant<InstR, InstFR, InstI, InstFI, InstJ, InstU,
                          PseudoR, PseudoLI, PseudoL, PseudoB, PseudoJ, PseudoRet>;
```

==== 模块结构

汇编程序组织为四层。`Module` 是顶层，输出时会按 `.rodata` → `.data` → `.bss` → `.text` 的顺序排列各节。`AsmFunc` 包含栈帧布局和基本块列表。`AsmBlock` 是标签加指令序列。`Global` 包含变量名、IR 类型、初始值和 `comptime` 属性，`is_zero_init()` 判断变量该放哪个节。`FrameLayout` 记录栈帧总大小和每个被 spill 的局部变量相对 `sp` 的偏移。`FloatLiteral` 存浮点常量，写进 `.rodata`。

```cpp
struct Module {
    std::vector<Global> globals;
    std::vector<AsmFunc> funcs;
    std::vector<FloatLiteral> float_literals;
};

struct AsmFunc {
    std::string name;
    FrameLayout frame;
    std::vector<AsmBlock> blocks;
};

struct AsmBlock {
    std::string label;
    std::vector<Inst> insts;
};
```

==== 操作码枚举

`op.hpp` 中按指令格式用 `enum class : uint8_t` 定义所有操作码。`OpR`（整数 R 型）含 ADD/SUB/MUL/DIV/REM/SLT/AND/OR/XOR/SLL/SRL/SRA 及对应 32 位变体。`OpFR`（浮点 R 型）含 FADD/FSUB/FMUL/FDIV/FEQ/FLT/FLE/FCVT 等。`OpI`（整数 I 型）含 ADDI/SLTI/XORI/ORI/ANDI 等算术立即数、LD/LW/LB/LBU/LH/LHU 等 Load、SD/SW/SB/SH 等 Store、BEQ/BNE/BLT/BGE 等 Branch。`OpFI` 是四条浮点 Load/Store，`OpJ` 仅 JAL，`OpU` 是 LUI/AUIPC。每个枚举配有 `toString()` 返回汇编助记符。

=== 指令选择

指令选择将 IR 指令翻译为 RISC-V 汇编。输入是寄存器分配后的 IR（每个操作数已绑定物理寄存器、spill 位置或常数值），输出是上节定义的 `Module`。

`lower()` 作为入口，先收集全局变量（跳过已分配寄存器的），再逐个翻译函数：

```cpp
Module lower(const Program& prog, const ColorMap& regs, const TargetABI& abi) {
    Module mod;
    for (auto& g : prog.globals()) {
        if (regs.count(g->value())) continue;
        mod.globals.emplace_back(g->name, g->type, g->init, g->comptime);
    }
    for (auto& f : prog.funcs())
        mod.funcs.emplace_back(translate_func(*f, abi, regs, mod.float_literals));
    return mod;
}
```

==== 函数翻译

`translate_func` 依次生成三段代码。prologue：函数标签 + 开辟栈帧（`addi sp, sp, -N`）+ 有初始值的局部变量的 Store。主体：遍历 IR 基本块，逐条翻译指令和出口，标签名用 `".L" + 函数名 + "_" + 块标签`。epilogue：释放栈帧 + `ret`，return 指令统一跳到这里。

```cpp
AsmFunc translate_func(const Func& func, ...) {
    AsmFunc af;
    af.frame = compute_frame(func, abi);

    // prologue
    AsmBlock entry;
    entry.label = func.name;
    if (af.frame.total_size > 0) emit_add_sp(entry, -(int64_t)af.frame.total_size);
    for (auto& local : func.locals()) { /* emit init stores */ }
    entry.insts.emplace_back(PseudoJ{J, entry_label});
    af.blocks.emplace_back(entry);

    // body
    for (auto& blk : func.blocks()) {
        AsmBlock ab;
        ab.label = block_label(func.name, blk->label);
        for (auto& inst : blk->insts()) translate_inst(inst, ab, ...);
        translate_exit(blk->exit(), ab, func.name, regs);
        af.blocks.emplace_back(ab);
    }

    // epilogue
    AsmBlock epi;
    epi.label = epilogue_label;
    if (af.frame.total_size > 0) emit_add_sp(epi, (int64_t)af.frame.total_size);
    epi.insts.emplace_back(PseudoRet{});
    af.blocks.emplace_back(epi);
    return af;
}
```

指令分发用 `Match` 做模式匹配：

```cpp
Match{inst}(
    [&](const UnaryInst& u)  { translate_unary(u, ...); },
    [&](const BinaryInst& b) { translate_binary(b, ...); },
    [&](const CallInst& c)   { translate_call(c, blk); },
    [&](const PhiInst&)      { /* 应在 isel 前被消除 */ }
);
```

几个辅助函数贯穿整个翻译过程。`lookup_reg(regs, val)` 从 ColorMap 查 IR 值的物理寄存器，找不到说明是常量或栈上变量。`extract_imm(cv)` 尝试从编译期常量提取整数，浮点或数组则失败。`fits_i12(imm)` 判断立即数是否在 12 位有符号范围（-2048 ~ 2047），决定能否用 I 型指令。`resolve_alloc(val)` 从 IR 值中提取底层 `Alloc*`，用来判断是栈变量还是全局变量。

==== 一元指令

MOV 需要处理四种操作数。立即数：整数走 `PseudoLI`，浮点不能立即编码，放进 `.rodata` 常量池再 `FLD/FLW` 加载。寄存器：整数用 `MV`（`addi rd, rs, 0`），浮点用 `FSGNJ.S rd, rs, rs`（符号注入的副作用等于复制）。栈变量：`ADDI rd, sp, offset`。全局：`comptime` 的如果是整数立即数直接 `LI`，否则 `LA` 取地址。

NEG：浮点用 `FSGNJN`（符号取反注入），整数用 `NEG`（`sub rd, zero, rs`）。若操作数是常量直接 `LI` 相反数。

NOT：`Int1`（布尔）用 `XORI rd, rs, 1`，其他类型用 `SEQZ`（`sltiu rd, rs, 1`）。常量情况直接 `LI` 取反值。

LOAD：根据引用元素类型选 Load 宽度——`Int1` 用 `LBU`，32 位用 `LW`/`FLW`，64 位用 `LD`/`FLD`。栈变量用 `sp+offset`，寄存器指针用 `0(base)`，全局用 `PseudoL(LGD/LGW)`。

CONVERT：整数到浮点走 `FCVT.S.W` 等，浮点到整数走 `FCVT.W.S` 等，浮点精度转换走 `FCVT.S.D`/`FCVT.D.S`，32 位整数扩展走 `ADDIW`。

==== 二元指令

STORE（`result` 为空）把值写入引用目标。同样分三种地址源——栈：`SW/SD sp+offset`；寄存器指针：`SW/SD 0(base)`；全局：`PseudoL(SGD/SGW)`。常量源操作数先具体化到 `t0`(x5) 或 `ft0`(f5)。字节数组初始化用 `emit_store_bytes` 按 4/2/1 字节分块 Store。

整数运算按操作数类型分级。双寄存器直接 R 型。寄存器加立即数，若立即数在 12 位范围内直接用 I 型（`ADDI`/`SLTI` 等），否则 `LI` 到临时寄存器再 R 型。`rd == lhs` 时用 `t0`(x5) 做中转避免覆盖。

比较运算的映射有些绕。RISC-V 只有 `SLT`（小于）和 `SLTU`（无符号小于）。`x > y` 即 `y < x`，交换操作数。`x >= y` 即 `!(x < y)`，用 `SLT + XORI 1`。`x == y` 用 `SUB + SEQZ`（`x-y == 0`）。`x != y` 用 `SUB + SNEZ`。立即数版本同理，能 `SLTI` 就不 `LI+SLT`。

左操作数是常量时，交换律成立的运算（ADD/MUL/AND/OR/EQ/NEQ）直接交换，不等价的（SUB/DIV/MOD）先 `LI` 加载常量再 R 型。比较中 `c < x` 即 `x > c`，走 `SLTI + XORI`。

浮点运算直接映射到 `FADD/FSUB/FMUL/FDIV.{S,D}`。GT/GEQ 交换操作数走 FLT/FLE。NEQ 走 `FEQ + XORI(rd, rd, 1)`。

==== 出口

Return 跳转到 epilogue。Jump 直接 `j label`。Branch 展开为 `bnez cond, true_label` + `j false_label`：

```cpp
void translate_exit(const Exit& exit, ...) {
    Match{exit}(
        [&](const ReturnExit&) { blk.insts.emplace_back(PseudoJ{J, epi_label}); },
        [&](const JumpExit& j) { blk.insts.emplace_back(PseudoJ{J, label_of(j.target)}); },
        [&](const BranchExit& b) {
            blk.insts.emplace_back(PseudoB{BNEZ, cond_reg, true_label});
            blk.insts.emplace_back(PseudoJ{J, false_label});
        }
    );
}
```

函数调用生成 `PseudoJ(CALL)`，汇编器展开为 `auipc ra, ...; jalr ra, ...`。返回值寄存器 `a0`/`fa0` 的赋值由寄存器分配阶段的预着色 Pass 完成，isel 不参与。

==== 栈帧计算

`compute_frame` 遍历局部变量，对有 `reference` 属性的（被 spill 的）分配栈空间。每个变量按其类型大小对齐（对齐等于大小，RISC-V 自然对齐），最终总大小按 16 字节对齐：

```cpp
FrameLayout compute_frame(const Func& func, const TargetABI& abi) {
    FrameLayout layout;
    size_t offset = 0;
    for (auto& local : func.locals()) {
        if (!local->reference) continue;
        size_t sz = abi.mem.size(local->type);
        size_t al = abi.mem.align(local->type);
        if (al > 1) offset = (offset + al - 1) & ~(al - 1);
        layout.spill_offsets[local.get()] = offset;
        offset += sz;
    }
    offset = (offset + abi.mem.stack_alignment - 1) & ~(abi.mem.stack_alignment - 1);
    layout.total_size = offset;
    return layout;
}
```

==== 立即数优化

I 型指令的立即数字段是 12 位有符号整数。能放进这个范围的常量，用一条 I 型指令替代 `LI + R 型` 两条指令：

```asm
// 优化前（2 条）:           // 优化后（1 条）:
li  t0, 100                  addi a0, a1, 100
add a0, a1, t0
```

比较运算中 `x >= N` 拆为 `!(x < N)`。若 N 在立即数范围，`SLTI + XORI` 两条完成；若超出，先 `LI` N 再 `SLT + XORI`。`x > N` 等价于 `x >= N+1`，N+1 在范围内同理优化，否则转 `N < x` 走 `LI + SLT`。

==== 浮点常量池

浮点常量（float 32 位、double 64 位）无法编码进 12 位立即数。我们为每个浮点常量在 `.rodata` 创建一个标签，用 `FLW`/`FLD` 加载：

```asm
    la  t0, .L_fc_0
    fld fa0, 0(t0)
    ...
.section .rodata
.balign 8
.L_fc_0:
    .dword 0x40091EB851EB851F   # 3.14
```

==== 临时寄存器

指令选择阶段保留两个临时寄存器：`t0`(x5) 和 `ft0`(f5)。它们在 ABI 定义里被标为 `reserved`，寄存器分配不会把它们分给变量。isel 中任何需要"先加载一个东西再做运算"的场景都用它们，用完即弃，不存在跨指令冲突。