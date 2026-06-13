#import "@preview/finite:0.5.1": automaton, layout
#import "../../preamble/preamble.typ": *
#import "@preview/algorithmic:1.0.7"
#import algorithmic: algorithm-figure, style-algorithm
#show: style-algorithm

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

  ```rust
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

```rust
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
      Fn("insert_copies")[_function.entry_]
    })
    LineBreak
    Function("insert_copies", [_block_], {
      Assign[_pushed_][$emptyset$]
      For([_i_ $in$ _block.instructions_], {
        For([_u_ $in$ _i.uses_], {
          Line[Replace all uses _u_ with _Stacks[u].top()_]
        })
      })
      Fn("schedule_copies", [_block_])
      For([_c_ $in$ _dominator_tree.children_of(block)_], {
        Fn("insert_copies")[_c_]
      })
      For([_n_ $in$ _pushed_], {
        Fn("pop")[_Stacks[n]_]
      })
    })
    LineBreak
    Function("schedule_copies", [_block_], {
      LineComment(Assign[copy_set][$emptyset$], [Pass One: Initialize the data structures])
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
      Fn("split_critical_edges")[_function_]
      Fn("insert_copies")[_function.entry_]
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
      Fn("schedule_copies", [_block_])
      For([_c_ $in$ _dominator_tree.children_of(block)_], {
        Fn("insert_copies")[_c_]
      })
    })
    LineBreak
    Function("schedule_copies", [_block_], {
      LineComment(Assign[copy_set][$emptyset$], [Pass One: Initialize the data structures])
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
  (strong("for") + " " + cond + " " + strong(kw) + " " + pattern + " " + strong("do")),
  (change-indent: 2, body: body.pos()),
  strong("end"),
)
#let ForMatch(cond, pattern, ..body) = ForWithCond("match", cond, pattern, ..body)
#let ForIf(cond, pattern, ..body) = ForWithCond("if", cond, pattern, ..body)

#algorithm-figure(
  "Array Item Address Lowering",
  inset: 0.25em,
  {
    import algorithmic: *
    Function("lower_address", [_block_], {
      ForMatch([_inst_ $in$ _block.instructions_], [#assign[_elem_addr_][& _array_[_index_]] ], {
        Line[create temp value _offset_ ]
        Fn[insert_before][_inst, #assign[offset][index \* array.type.elem.size]_]
        Fn[replace][_inst, #assign[elem_addr][array + offset]_]
      })
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
