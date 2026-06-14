#import "@preview/finite:0.5.1": automaton, layout
#import "../../preamble/preamble.typ": *
#import "@preview/algorithmic:1.0.7"
#import algorithmic: algorithm-figure, style-algorithm
#show: style-algorithm
#import "@preview/tablem:0.3.0": three-line-table

=== IR 内部的降级 (Lowering)

==== 消除 phi 节点

消除 phi 节点的核心是对于每一个phi指令，找到它的前驱，并在每个前驱块末尾插入一条 `copy` 指令

但可能存在问题，考虑这样一个 cfg:

#figure(
  automaton(
    (
      entry: (bb1: "", bb2: ""),
      bb1: (bb3: ""),
      bb2: (bb3: "", bb4: ""),
    ),
    final: none,
    layout: layout.grid.with(columns: 2),
    style: (transition: (curve: 0), bb2-bb3: (stroke: red)),
  ),
  caption: "critical edge",
)

假设现在现在需要消除 bb3 中的 phi ，trivial 的做法是直接在 bb1 和 bb2 末尾插入 copy，但这会导致 bb2 $->$ bb4 的路径中也执行这个 copy，这是不正确的，这被称为 critical edge 问题

#definition(title: "critical edge")[
  critical edge 是一条起点有多个后继、终点有多个前驱的边
]

还有一个问题，考虑以下 IR

#grid(columns: 2, gutter: 1em)[

  ```riir
  fn gcd(m: i32, n: i32) -> i32 {
      'entry: {
          => 'cond;
      }
      'cond: {
          %n.0: i32 = phi('loop: %1,   'entry: @n);
          %m.0: i32 = phi('loop: %n.0, 'entry: @m);
          %0: bool = %n.0 > 0;
          => if %0 { 'loop } else { 'exit };
      }
      'loop: {
          %1: i32 = %m.0 % %n.0;
          => 'cond;
      }
      'exit: {
          return %m.0;
      }
  }
  ```
][
  （从以下代码生成）
  ```c
  int gcd(int m, int n) {
      while (n > 0) {
          int t = m % n;
          m = n;
          n = t;
      }
      return m;
  }
  ```
]

简单地将phi替换为赋值指令得到 loop 基本块如下：

```riir
'loop: {
    %1: i32 = %m.0 % %n.0;
    %n.0: i32 = %1;
    %m.0: i32 = %n.0;
    => 'cond;
}
```

可以看到，`%n.0` 的值被错误覆盖了，这被称为 swap problem @briggsPracticalImprovementsConstruction1998

解决这个问题的核心在于 schedule 出一个合适的赋值顺序，必要时引入临时变量

Briggs 等人 @briggsPracticalImprovementsConstruction1998 提出了一个算法解决以上两个问题，原始算法如下：

#let encode(x) = [$chevron.l$#x$chevron.r$]
#let pair(a, b) = encode[#a, #b]
#let assign(a, b) = encode[#a $:=$ #b]
#let store(a, b) = encode[#a $<-$ #b]
#let sdpair = pair[_src_][_dest_]

#algorithm-figure(
  "Replace Phi Nodes",
  inset: 0.25em,
  {
    import algorithmic: *
    Function("replace_phi_nodes", [_function_], {
      Line[Perform liveness analysis]
      For([_v_ $in$ _function.variables_], {
        Assign[_Stacks[n]_][${}$]
      })
      Call("insert_copies")[_function.entry_]
    })
    LineBreak
    Function("insert_copies", [_block_], {
      Assign[_pushed_][$emptyset$]
      For([_i_ $in$ _block.instructions_], {
        For([_u_ $in$ _i.uses_], {
          Line[Replace all uses _u_ with _Stacks[u].top()_]
        })
      })
      Call("schedule_copies", [_block_])
      For([_c_ $in$ _dominator_tree.children_of(block)_], {
        Call("insert_copies", [_c_])
      })
      For([_n_ $in$ _pushed_], {
        Fn("pop")[_Stacks[n]_]
      })
    })
    LineBreak
    Function("schedule_copies", [_block_], {
      LineComment(
        Assign[copy_set][$emptyset$],
        [Pass One: Initialize the data structures],
      )
      For([_dest_ $<- phi.alt$(_args_) $in$ _block.$phi.alt$-functions_], {
        For([_src_, _succ_ $in$ _args_], {
          Assign[_copy\_set_][_copy\_set_ $union { sdpair }$]
          Assign[_map[src]_][_src_]
          Assign[_map[dest]_][_dest_]
          Assign[_used_by_another[src]_][TRUE]
        })
      })
      LineComment(
        For([#sdpair $in$ _copy_set_], {
          If([$not$ _used_by_another[dest]_], {
            Assign[_worklist_][_worklist_ $union { sdpair }$]
            Assign[_copy_set_][_copy_set_ $\\ { sdpair }$]
          })
        }),
        [Pass Two: Set up the worklist of initial copies],
      )
      LineComment(
        While($#[_worklist_] != emptyset or #[_copy_set_] != emptyset$, {
          While($#[_worklist_]!= emptyset$, {
            Line[Pick a #sdpair from _worklist_]
            Assign[_worklist_][$#[_worklist_] \\ { sdpair }$]
            If($#[_dest_] in #[_live_out_] _b$, {
              Line[Insert a copy from _dest_ to a new temp _t_ at $phi.alt$-node defining _dest_]
              Fn("push", ([_t_], [_Stacks[dest]_]))
            })
            Line[Insert a copy operation from _map[src]_ to _dest_ at the end of _b_]
            Assign[_map[src]_][_dest_]
            If([_src_ $in$ _copy_set.destinations_], {
              Line[Add that copy to worklist]
            })
          })
          If($#[_copy_set_] != emptyset$, {
            Line[Pick a #sdpair from copy_set]
            Assign[_copy_set_][$#[_copy_set_] \\ { sdpair }$]
            Line[Insert a copy from _dest_ to a new temp _t_ at the end of _b_]
            Assign[_map[dest]_][$t$]
            Assign[_worklist_][$#[_worklist_] union { sdpair }$]
          })
        }),
        [Pass Three: Iterate over the worklist, inserting copies],
      )
    })
  },
)

我们实际使用的算法进行了一些修改

原算法中其中活跃变量分析和 Stack 维护都是为了解决 critical edge 问题，既然它是 cfg 的问题，那我们就从cfg角度解决它。

根本上的问题在于 phi 语义上来说是在 cfg 的边上执行的，而不是节点中执行，因此在 phi-elimination 过程中，我们对于 critical edge 创建一个新的节点，然后再进行朴素的 「在每一个前驱插入copy」算法，就能避免 critical edge 问题

因此，我们的算法如下：

#algorithm-figure(
  "Replace Phi Nodes (Modified)",
  inset: 0.25em,
  {
    import algorithmic: *
    Function("replace_phi_nodes", [_function_], {
      Call("split_critical_edges")[_function_]
      Call("insert_copies", [_function.entry_])
    })
    LineBreak
    Function("split_critical_edges", [_function_], {
      For($sdpair in #[_function.cfg.critical_edges_]$, {
        If($#[_dest.$phi.alt$-nodes_] != emptyset$, {
          Line[Insert a new block _b_ between _src_ and _dest_ ]
          Line[Redirect edges to _dest_ to _b_ and $phi.alt$-nodes from _src_ to from _b_]
        })
      })
    })
    LineBreak
    Function("insert_copies", [_block_], {
      Call("schedule_copies", [_block_])
      For([_c_ $in$ _dominator_tree.children_of(block)_], {
        Call("insert_copies")[_c_]
      })
    })
    LineBreak
    Function("schedule_copies", [_block_], {
      LineComment(
        Assign[copy_set][$emptyset$],
        [Pass One: Initialize the data structures],
      )
      let sdpair = pair("src", "dest")
      For([_dest_ $<- phi.alt$(_args_) $in$ _block.$phi.alt$-functions_], {
        For([_src_, _succ_ $in$ _args_], {
          Assign[_copy\_set_][_copy\_set_ $union { sdpair }$]
          Assign[_map[src]_][_src_]
          Assign[_map[dest]_][_dest_]
          Assign[_used_by_another[src]_][TRUE]
        })
      })
      LineComment(
        For([#sdpair $in$ _copy_set_], {
          If([$not$ _used_by_another[dest]_], {
            Assign[_worklist_][_worklist_ $union { sdpair }$]
            Assign[_copy_set_][_copy_set_ $\\ { sdpair }$]
          })
        }),
        [Pass Two: Set up the worklist of initial copies],
      )
      LineComment(
        While($#[_worklist_] != emptyset or #[_copy_set_] != emptyset$, {
          While($#[_worklist_]!= emptyset$, {
            Line[Pick a #sdpair from _worklist_]
            Assign[_worklist_][$#[_worklist_] \\ { sdpair }$]
            Line[Insert a copy operation from _map[src]_ to _dest_ at the end of _b_]
            Assign[_map[src]_][_dest_]
            If([_src_ $in$ _copy_set.destinations_], {
              Line[Add that copy to worklist]
            })
          })
          If($#[_copy_set_] != emptyset$, {
            Line[Pick a #sdpair from copy_set]
            Assign[_copy_set_][$#[_copy_set_] \\ { sdpair }$]
            Line[Insert a copy from _dest_ to a new temp _t_ at the end of _b_]
            Assign[_map[dest]_][$t$]
            Assign[_worklist_][$#[_worklist_] union { sdpair }$]
          })
        }),
        [Pass Three: Iterate over the worklist, inserting copies],
      )
    })
  },
)

==== 计算内存访问地址

对于数组访问，我们的IR中存在 `BORROR_ELEM/BORROW_ELEM_MUT` 指令，用于从数组下标计算数组元素地址，然而这依赖于数组的类型信息，rv64汇编中也不存在直接的数组访问指令，需要把数组元素的大小算出来乘下标得到偏移量，从偏移量和基地址得到元素地址。

#let ForWithCond(kw, cond, pattern, ..body) = (
  (
    strong("for") + " " + cond + " " + strong(kw) + " " + pattern + " " + strong("do")
  ),
  (change-indent: 2, body: body.pos()),
  strong("end"),
)
#let ForMatch(cond, pattern, ..body) = ForWithCond(
  "match",
  cond,
  pattern,
  ..body,
)
#let ForIf(cond, pattern, ..body) = ForWithCond("if", cond, pattern, ..body)
#let ForMatchItem(cond, name, pattern, ..body) = (
  (
    strong("for")
      + " "
      + cond
      + " "
      + strong("if")
      + " "
      + name
      + " "
      + strong("match")
      + " "
      + pattern
      + " "
      + strong("do")
  ),
  (change-indent: 2, body: body.pos()),
  strong("end"),
)

#algorithm-figure(
  "Array Item Address Lowering",
  inset: 0.25em,
  {
    import algorithmic: *
    Function("lower_address", [_block_], {
      ForMatch(
        [_inst_ $in$ _block.instructions_],
        [#assign[_elem_addr_][& _array_[_index_]] ],
        {
          Line[create temp value _offset_ ]
          Fn[insert_before][_inst, #assign[offset][index \* array.type.elem.size]_]
          Fn[replace][_inst, #assign[elem_addr][array + offset]_]
        },
      )
    })
  },
)

==== 处理数组初始化

我们将操作数类型为 `Array` 的 `STORE` 指令转换为 `memset/memcpy`

前端生成IR时能保证这样的 `STORE` 源操作数一定是编译器常量，因此我们不考虑 `STORE` 的源操作数是变量的情况

#algorithm-figure(
  "Array Initialization Lowering",
  inset: 0.25em,
  {
    import algorithmic: *
    Function("lower_array_init", ([_program_], [_block_]), {
      ForMatch([_inst_ $in$ _block.instructions_], [#store[_dst_][_src_]], {
        If([_src_.type $=$ Array], {
          Assign[_pruned_][_src.trim_trailing_zeros()_]
          If([_pruned.size_ $=$ 0], {
            Fn[replace][_inst, #encode[#call("memset", [dst, 0, src.size], inline: true)]_]
          })
          Else({
            Line[create a unique identifier name _name_]
            Line[_program.add_const_global(name, pruned)_]
            If([_pruned.size_ $!=$ _src.size_], {
              Fn[insert_before][_inst, #encode[#call("memset", [dst, 0, src.size], inline: true)]_]
            })
            Fn[replace][_inst, #encode[#call("memcpy", [dst, name, pruned.size], inline: true)]_]
          })
        })
      })
    })
  },
)

==== 寄存器分配

我们选用图着色算法进行寄存器分配，算法核心思想是将变量的 live range 视为图中的节点，变量之间存在冲突（即 live range 有交集）则在它们之间连一条边，那么问题就转化为图的着色问题

通过预着色过程，图着色能自然地处理后端的abi要求，而不需要在寄存器分配时引入额外的 if-else 逻辑处理。

由于我们想要复用 IR 的分析基础设施（活跃变量分析等），因此我们选择直接在 IR 上进行寄存器分配，保留一个寄存器用于指令选择阶段的临时变量，其他寄存器用于分配给 IR 变量

实现上来说，我们在现有的 IR 类型系统中引入新的类型 `Int, Float`，表示通用寄存器和浮点寄存器对应的类型，修改后 IR 的 Primitive Type 包含 `Int1, Int32, Int, Float32, Float64, Float`。同时我们为每一个寄存器创建一个全局变量（属性：`-comptime, -ref, +mut`），通过全局变量模拟寄存器行为

寄存器分配前
```riir
fn main() -> i32 {
  'entry: {
      %0: i32 = @get_int();
      @print_int(%0);
      return 0;
  }
}
```
寄存器分配后
```riir
let mut __reg_ra: int;
let mut __reg_a0: int;

fn main() -> i32 {
    let ref mut __spill: int;
    'entry: {
        @__spill <- @__reg_ra;
        @__reg_a0: int = @get_int();
        @print_int(@__reg_a0);
        @__reg_a0: int = 0;
        @__reg_ra: int = * (&mut int)@__spill;
        return @__reg_a0;
    }
}
```

我们实现的寄存器分配大体上分为4个pass：

#enum[
  将不可分配寄存器的变量 spill 掉 `(RegToMem)`

  具体地，我们 spill 全局变量和超过 ABI 函数寄存器传参数量的参数。实现上只需要将 IR 中对应变量的 `Alloc` 打上 `ref` 属性，然后在每个使用点把直接对值的使用改成 `LOAD/STORE`

  #v(1em)
][
  预着色 `(Precolorize)`

  在这一个阶段，我们为每一个寄存器创建对应的全局变量，然后维护一个预着色表，将他们预着色成对应的寄存器编号

  随后，我们根据 ABI 要求修改IR中的函数参数、返回值、Call 指令使用的参数，使用预着色的节点

  同时，我们为 ABI 中规定的每一个 callee-saved 寄存器插入一个保存/恢复的临时变量。理想情况下，这个临时变量被着色为对应的寄存器，在最后的冗余 copy 消除阶段被优化掉，否则寄存器分配过程中会选择spill掉这个临时变量，从而实现了自动根据寄存器压力选择是否栈上保存/恢复 callee-saved 寄存器的效果。

  #algorithm-figure(
    "Precolorization",
    inset: 0.25em,
    {
      import algorithmic: *
      Function("precolorize", [_program_], {
        Line[Create precolored nodes for registers and add to precoloring table]
        For([_func_ $in$ _program.functions_], {
          Call[precolorize_param][_func_]
          For([_block_ $in$ _func.blocks_], {
            Call[precolorize_call][_block_]
          })
          Call[precolorize_return][_func_]
          Call[save_callee_registers][_func_]
        })
      })
      LineBreak
      Function("precolorize_param", [_func_], {
        For([_param_, _reg_id_ $in$ enumerate(_func.register_params_)], {
          Fn[prepend][_func.entry_, #assign[_param_][_registers[reg_id]_]]
        })
      })
      LineBreak
      Function("precolorize_return", [_func_], {
        ForMatchItem(
          [_insts_, _exit_ $in$ _func.blocks_],
          [_exit_],
          [return _retval_],
          {
            Assign[_reg_id_][_ABI.return_register(retval.type)_]
            Fn[append][_insts_, #assign[retval][_registers[reg_id]_]]
            Fn[replace][_exit_, #encode[return _register[reg_id]_]]
          },
        )
      })
      LineBreak
      Function("save_callee_registers", [_func_], {
        For([_reg_id_ $in$ _ABI.callee_saved_registers_], {
          Line[Create a temp variable _temp_ for saving the register]
          Fn[prepend][_func.entry_, #assign[_temp_][_registers[reg_id]_]]
          ForMatchItem(
            [_insts_, _exit_ $in$ _func.blocks_],
            [_exit_],
            [return _retval_],
            {
              Fn[append][_insts_, #assign[_registers[reg_id]_][_temp_]]
            },
          )
        })
      })
      LineBreak
      Function("precolorize_call", [_block_], {
        ForMatch(
          [_inst_ $in$ _block.instructions_],
          assign[_result_][_callee(args)_],
          {
            For([_arg_, _reg_id_ $in$ register_arguments(args)], {
              Fn[insert_before][_inst_, #assign[_registers[reg_id]_][_arg_]]
              Fn[replace][_arg_, _registers[reg_id]_]
            })
            If([_result_ $!=$ none], {
              Assign[_reg_id_][_ABI.return_register(result.type)_]
              Fn[insert_after][_inst_, #assign[_result_][_registers[reg_id]_]]
              Fn[replace][_result_, _registers[reg_id]_]
            })
          },
        )
      })
    },
  )

  #v(1em)
][
  构建冲突图、图着色 `(Colorize)`

  #enum[
    冲突图

    冲突图的节点需要维护干涉关系以及传送关系和动态度数，定义如下

    ```cpp
    struct InterfereNode {
        LeftValue value;
        std::set<LeftValue> interfere;
        std::set<LeftValue> move;
        size_t degree;
        std::optional<size_t> color;
    };
    ```

    构建冲突图时，我们首先进行一次基本块级别的活跃变量分析，然后在在每个基本块中倒序遍历指令，得到指令级别的live-out集合，然后在指令的 def 集合和 live-out 集合的笛卡尔积上连干涉边

    普通指令的 def 集合就是指令的目标变量，对于函数调用指令，除了目标变量以外，`ra` 寄存器也是一个隐式的 def，因为函数调用会修改 `ra` 寄存器的值。

    对于函数调用指令，我们还需要对 live-out 集合和 caller-saved 寄存器集合的笛卡尔积之间连干涉边，表示这些跨函数调用活跃的值不能分配到 caller-saved 寄存器上

    注意我们没有显式地为非叶子函数创建保存/恢复 `ra` 以及其他 caller-saved 寄存器的逻辑，而是在寄存器分配框架中自动完成。

    对于每一条传送指令 #assign[_dest_][_src_]，我们在 _dest_ 和 _src_ 之间连一条传送边，表示这两个变量应尽可能被分配到同一个寄存器上
    #v(1em)
  ][
    图着色

    我们实现了虎书 @appel2002modern 中描述的 Briggs 图着色算法

    算法维护多个工作列表，每个工作列表对应一类节点或传送指令的状态，每个节点或者传送指令在且仅在一个工作列表中。

    #figure(
      three-line-table[
        | 节点 worklist | 含义 |
        | --- | --- |
        | `precolored` | 预着色节点（机器寄存器） |
        | `initial` | 初始节点，尚未处理 |
        | `simplify` | 低度数、非传送相关节点，可安全移除 |
        | `freeze` | 低度数、传送相关节点，暂时不移除（寻找可能的合并） |
        | `to_spill` | 高度数节点，潜在溢出候选 |
        | `spilled` | 已确定为溢出的节点 |
        | `coalesced` | 已合并的节点 |
        | `colored` | 成功着色的节点 |
        | `select_stack` | 简化阶段已移除的节点栈，用于选择阶段着色 |
      ],
      caption: "Node Worklists",
    )

    #figure(
      three-line-table[
        | 传送 worklist | 含义 |
        | `coalesced` | 已合并的传送 |
        | `constrained` | 无法合并的传送（源/目标干涉） |
        | `frozen` | 已冻结的传送（放弃合并） |
        | `ready` | 待处理的传送 |
        | `scheduled` | 推迟处理的传送 |
      ],
      caption: "Move Worklists",
    )

    一个节点传送相关指存在一个 `ready/schedule` 的传送使得它是这个传送的一个操作数

    算法的状态转移如下：

    #figure(
      automaton(
        (
          build: (simplify: none),
          simplify: (coalesce: none, simplify: none),
          coalesce: (freeze: none, simplify: none),
          freeze: (potential_spill: none, simplify: none),
          potential_spill: (select: none, simplify: none),
          select: (select: none, actual_spill: none),
          actual_spill: (select: none, build: none),
        ),
        initial: none,
        style: (
          state: (stroke: 0pt, radius: 0.5),
          actual_spill-build: (curve: 2),
        ),
      ),
      caption: "Briggs Graph Coloring State Transitions",
    )

    #algorithm-figure(
      "Briggs Graph Coloring",
      inset: 0.25em,
      {
        import algorithmic: *
        Function("colorize", (), {
          Call("initialize")[]
          While(
            $#[_simplify_] != emptyset or #[_freeze_] != emptyset or #[_to_spill_] != emptyset or #[_ready_] != emptyset$,
            {
              If($#[_simplify_] != emptyset$, {
                Call("simplify")[]
              })
              ElseIf($#[_ready_] != emptyset$, {
                Call("coalesce")[]
              })
              ElseIf($#[_freeze_] != emptyset$, {
                Call("freeze")[]
              })
              ElseIf($#[_to_spill_] != emptyset$, {
                Call("spill")[]
              })
            },
          )
          Call("assign_colors")[]
        })
        LineBreak
        Function("initialize", (), {
          For([_n_ $in$ _initial_], {
            If([_n_.degree $>$ _K_], {
              Line[Move _n_ to _to_spill_]
            })
            ElseIf([_move_related(n)_], {
              Line[Move _n_ to _freeze_]
            })
            Else({
              Line[Move _n_ to _simplify_]
            })
          })
        })
        LineBreak
        Function("adjacent", [_n_], {
          Return[_n.adjacent_nodes_ $\\$ (_select_stack_ $union$ _coalesced_ )]
        })
        LineBreak
        Function("simplify", (), {
          Line[Remove node _n_ from _simplify_]
          Line[Push _n_ onto _select_stack_]
          Assign([_adj_], Call[adjacent][_n_])
          For([_m_ $in$ _adj_], {
            Line[Decrement degree of _m_]
            Line[If _m_.degree < _K_, enable moves for _m_]
          })
        })
        LineBreak
        Function("coalesce", (), {
          Line[Pick move _m = (x, y)_ from _ready_]
          Assign[x][_alias(x)_]
          Assign[y][_alias(y)_]
          If([_x_ = _y_], {
            Line[Move _m_ to _coalesced_]
            Call("unfreeze")[_x_]
          })
          ElseIf([_x_ $in$ _precolored_ and _interferes(x, y)_], {
            Line[Move _m_ to _constrained_]
            Call("unfreeze")[_x_]
            Call("unfreeze")[_y_]
          })
          ElseIf([_mergable(x, y)_], {
            Line[Move _m_ to _coalesced_]
            Line[Merge _y_ into _x_]
            Call("unfreeze")[_x_]
          })
          Else({
            Line[Move _m_ to _scheduled_]
          })
        })
        LineBreak
        Function("freeze", (), {
          Line[Remove node _n_ from _freeze_]
          Line[Move _n_ to _simplify_]
          For([_m_ $in$ _moves(n)_], {
            Line[Move _m_ to _frozen_]
            Assign[src][_alias(m.source)_]
            If(
              [_src_ $in$ _freeze_ and _src_.degree $< K and not$ _move_related(src)_],
              {
                Line[Move _src_ to _simplify_]
              },
            )
          })
        })
        LineBreak
        Function("spill", (), {
          Line[Select node _n_ with lowest priority from _to_spill_]
          Line[Move _n_ to _simplify_]
          For([_m_ $in$ _moves(n)_], {
            Line[Move _m_ to _frozen_]
          })
        })
        LineBreak
        Function("assign_colors", (), {
          While($#[_select_stack_] != emptyset$, {
            Line[Pop node _n_ from _select_stack_]
            Line[Find available colors not used by neighbors]
            If([_available_colors_ $= emptyset$], {
              Line[Move _n_ to _spilled_]
            })
            Else({
              Line[Select a best color _c_ from _available_colors_]
              Assign[_color(n)_][_c_]
              Line[Move _n_ to _colored_]
            })
          })
          For([_n_ $in$ _coalesced_], {
            Assign[color][_color(alias(n))_]
            If([_color_ exists], {
              Line[Assign _color_ to _n_]
            })
            Else({
              Line[Move _n_ to _spilled_]
            })
          })
        })
      },
    )

    其中 `mergable(x, y)` 的判断使用 George 和 Briggs 合并启发式：

    1. 若对于 _y_ 的每个邻居 _t_，都有 _t_ 已着色/度数小于 _K_ 或与 _x_ 干涉，则可以合并。
    2. 若合并后高度数 $(>= K)$ 邻居不超过 $K$ 个则可以合并

    两种启发式合并的原理都是保证合并前的图可着色蕴含合并后的图可着色。

    在简化阶段，移除节点时需要更新邻居节点的度数。若节点度数降至 _K_ 以下，需要将其从 `to_spill` 移回 `freeze` 或 `simplify`，并重新激活与之相关的传送指令。

    合并阶段的目标是消除传送指令：若源和目标变量可以合并为一个变量，它们会被分配相同的寄存器。合并条件保证了合并不会导致图变得不可着色。

    选择阶段从栈中弹出节点并着色。对于每个节点，选择一个未被邻居使用的颜色。我们优先选择与传送相关节点相同的颜色，以减少传送指令。若没有可用颜色，节点被标记为溢出。

    溢出的节点需要在下一轮迭代中处理：我们会插入实际的 spill 代码并进行 load/store 替换，然后重新构建冲突图并执行着色算法。

    实现上，我们为通用寄存器和浮点寄存器维护两张独立的干涉图，进行两次着色。
    #v(1em)
  ][
    示例

    考虑以下代码：
    ```riir
    fn f(a: i32, b: i32) -> i32 {
        let mut d: i32;
        let mut e: i32;
        'entry: {
            @d: i32 = 0;
            @e: i32 = @a;
            => 'cond;
        }
        'cond: {
            %0: bool = @e > 0;
            => if %0 { 'then } else { 'exit };
        }
        'then: {
            @d: i32 = @d + @b;
            @e: i32 = @e - 1;
            => 'cond;
        }
        'exit: {
            return @d;
        }
    }
    ```

    寄存器ABI：（`r0/r1/f0/f1: caller-saved, as arguments, r2/f2: callee saved`）
    ```cpp
    RegisterABI regs = {.size = 3,
                        .caller_saved = {0, 1},
                        .callee_saved = {2},
                        .reserved = {},
                        .parameters = {0, 1},
                        .return_value = 0};
    ```

    预着色后：
    ```riir
    let mut __reg_r0: int;
    let mut __reg_r1: int;
    let mut __reg_r2: int;
    let mut __reg_f0: float;
    let mut __reg_f1: float;
    let mut __reg_f2: float;

    fn f(a: i32, b: i32) -> i32 {
        let mut d: i32;
        let mut e: i32;
        'entry: {
            %2: float = @__reg_f2;
            %1: int = @__reg_r2;
            @b: i32 = @__reg_r1;
            @a: i32 = @__reg_r0;
            @d: i32 = 0;
            @e: i32 = @a;
            => 'cond;
        }
        'cond: {
            %0: bool = @e > 0;
            => if %0 { 'then } else { 'exit };
        }
        'then: {
            @d: i32 = @d + @b;
            @e: i32 = @e - 1;
            => 'cond;
        }
        'exit: {
            @__reg_r0: int = @d;
            @__reg_r2: int = %1;
            @__reg_f2: float = %2;
            return @__reg_r0;
        }
    }
    ```

    初始的通用寄存器干涉图（实线：干涉，虚线：传送）：

    #figure(image("image.png", width: 50%), caption: "Initial Interference Graph")

    第一遍着色后：

    #figure(image("image-1.png", width: 50%), caption: "First Pass Coloring Result")

    存在无法着色的节点 `%1, @b`，我们将它们 spill 掉，结果：

    ```riir
    let mut __reg_r0: int;
    let mut __reg_r1: int;
    let mut __reg_r2: int;
    let mut __reg_f0: float;
    let mut __reg_f1: float;
    let mut __reg_f2: float;

    fn f(a: i32, ref b: i32) -> i32 {
        let mut d: i32;
        let mut e: i32;
        let ref mut __spill_1: int;
        'entry: {
            %2: float = @__reg_f2;
            %3: int = @__reg_r2;
            @__spill_1 <- %3;
            %4: i32 = @__reg_r1;
            @b <- %4;
            @a: i32 = @__reg_r0;
            @d: i32 = 0;
            @e: i32 = @a;
            => 'cond;
        }
        'cond: {
            %0: bool = @e > 0;
            => if %0 { 'then } else { 'exit };
        }
        'then: {
            %5: i32 = * (&i32)@b;
            @d: i32 = @d + %5;
            @e: i32 = @e - 1;
            => 'cond;
        }
        'exit: {
            @__reg_r0: int = @d;
            %6: int = * (&mut int)@__spill_1;
            @__reg_r2: int = %6;
            @__reg_f2: float = %2;
            return @__reg_r0;
        }
    }
    ```

    重新分配寄存器，此时分配成功

    #figure(image("image-2.png", width: 60%), caption: "Final Coloring Result")
  ]


  #v(1em)
][
  去掉冗余的 copy 指令 `(RedundantMoveElimination)`

  将分配了寄存器的值替换为对应的寄存器后，删去所有 source 和 destination 是同一个寄存器的 copy 指令

  #v(1em)
]
