== 遇到的问题

一个有意思的bug：

=== phi 的并行语义

考虑以下IR：

#grid(columns: 2, gutter: 1em)[
  ```rust
  fn mod() {
      let mut rem: i32;
      let mut m: i32;
      let mut n: i32;
      'entry: {
          @m: i32 = 10;
          @n: i32 = 3;
          => 'loop;
      }
      'loop: {
          @rem: i32 = @m % @n;
          @m: i32 = @n;
          @n: i32 = @rem;
          => 'loop;
      }
  }
  ```
][
  ```c
    void mod() {
        int m = 10, n = 3, rem;
        while (1) {
            rem = m % n;
            m = n;
            n = rem;
        }
    }
  ```
]
---

变换成 SSA 形式：

```rust
fn mod() {
    let rem: i32;
    let m: i32;
    let n: i32;
    'entry: {
        %m.0: i32 = 10;
        %n.0: i32 = 3;
        => 'loop;
    }
    'loop: {
        %n.1: i32 = phi('entry: %n.0, 'loop: %n.2);
        %m.1: i32 = phi('entry: %m.0, 'loop: %m.2);
        %rem.0: i32 = %m.1 % %n.1;
        %m.2: i32 = %n.1;
        %n.2: i32 = %rem.0;
        => 'loop;
    }
}
```

---

复制传播+死代码消除：

```rust
fn mod() {
    let rem: i32;
    let m: i32;
    let n: i32;
    'entry: {
        %m.0: i32 = 10;
        %n.0: i32 = 3;
        => 'loop;
    }
    'loop: {
        %n.1: i32 = phi('entry: %n.0, 'loop: %rem.0);
        %m.1: i32 = phi('entry: %m.0, 'loop: %n.1);
        %rem.0: i32 = %m.1 % %n.1;
        => 'loop;
    }
}
```

如果 VM 按*顺序*执行 Phi 指令且 `%n.1` 的 Phi 先被处理，那么 `%m.1` 的 Phi 会读取到 `%n.1` *更新后*的值，而不是前驱块传来的旧值。

---

==== 分析

解决方法是让 Phi 指令带有一种“同时执行”的语义，要求所有的 Phi 指令在基本块开头被同时处理，读取前驱块传来的值，而不是一个一个执行。

跟老师讨论之后知道了这个问题被 1997 年 Briggs等人的论文 _Practical Improvements to the Construction and Destruction of Static Single Assignment Form_ 中被命名为 "swap problem"

事实上，一些 SSA IR （MLIR, Switft IR）不使用 Phi 指令，而是将基本块视为一个函数，通过基本块参数实现值的合并，传参天然具有同时赋值的语义，对应 IR 如下：

```rust
    'entry() {
        %m.0: i32 = 10;
        %n.0: i32 = 3;
        => 'loop(%n.0, %m.0);
    }
    'loop(%n.1: i32, %m.1: i32) {
        %rem.0: i32 = %m.1 % %n.1;
        => 'loop(%rem.0, %n.1);
    }
```

reference: https://www.cs.cornell.edu/courses/cs6120/2025sp/lesson/6/