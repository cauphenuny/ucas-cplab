== 优化效果

Source

#grid(columns: 2, gutter: 1em)[
  ```c
  // test if-else-if
  int ifElseIf() {
    int a; a = 5;
    int b; b = 10;
    if(a == 6 || b == 0xb) { return a; }
    else {
      if (b == 10 && a == 1)
        a = 25;
      else if (b == 10 && a == -5)
        a = a + 15;
      else
        a = -+a;
    }
    return a;
  }
  int main(){
    print_int(ifElseIf());
    return 0;
  }
  ```
][
  ```bash
  $build/compiler test/samples_codegen_functional/029_if_test2.cact --exec
  -5
  Program returned 0 after executing 27 instructions
  ```
]

---

#let small(content) = [
  #show raw.where(block: true): text.with(size: 0.8em)
  #content
]

Generated IR:
#small[
  #grid(columns: 3)[
    ```rust
      fn ifElseIf() -> i32 {
          let mut a_0: i32;
          let mut b_0: i32;
          'entry: {
              @a_0: i32 = 0;
              @a_0: i32 = 5;
              @b_0: i32 = 0;
              @b_0: i32 = 10;
              %0: bool = @a_0 == 6;
              => if %0 { 'if_true_7_2 } else { 'L0 };
          }
          'if_true_7_2: {
              return @a_0;
          }
          'if_exit_7_2: {
              return @a_0;
          }
          'if_false_7_2: {
              %2: bool = @b_0 == 10;
              => if %2 { 'L1 } else { 'if_false_11_4 };
          }
          'L0: {
              %1: bool = @b_0 == 11;
    ```
  ][
    ```rust
              => if %1 { 'if_true_7_2 } else { 'if_false_7_2 };
          }
          'if_true_11_4: {
              @a_0: i32 = 25;
              => 'if_exit_11_4;
          }
          'if_exit_11_4: {
              => 'if_exit_7_2;
          }
          'if_false_11_4: {
              %4: bool = @b_0 == 10;
              => if %4 { 'L2 } else { 'if_false_13_9 };
          }
          'L1: {
              %3: bool = @a_0 == 1;
              => if %3 { 'if_true_11_4 } else { 'if_false_11_4 };
          }
          'if_true_13_9: {
              %7: i32 = @a_0 + 15;
              @a_0: i32 = %7;
              => 'if_exit_13_9;
          }
    ```
  ][
    ```rust
        'if_exit_13_9: {
            => 'if_exit_11_4;
        }
        'if_false_13_9: {
            %8: i32 = @a_0;
            %9: i32 = - %8;
            @a_0: i32 = %9;
            => 'if_exit_13_9;
        }
        'L2: {
            %5: i32 = - 5;
            %6: bool = @a_0 == %5;
            => if %6 { 'if_true_13_9 } else { 'if_false_13_9 };
        }
    }

    fn main() -> i32 {
        'entry: {
            %0: i32 = @ifElseIf();
            %1: () = @print_int(%0);
            return 0;
        }
    }
    ```
  ]
]

---

\#0 SSA Form:

#small[
  #grid(columns: 3)[
    ```rust
      fn ifElseIf() -> i32 {
          let a_0: i32;
          let b_0: i32;
          'entry: {
              %a_0.0: i32 = 0;
              %a_0.1: i32 = 5;
              %b_0.0: i32 = 0;
              %b_0.1: i32 = 10;
              %0: bool = %a_0.1 == 6;
              => if %0 { 'if_true_7_2 } else { 'L0 };
          }
          'if_true_7_2: {
              return %a_0.1;
          }
          'if_exit_7_2: {
              return %a_0.2;
          }
          'if_false_7_2: {
              %2: bool = %b_0.1 == 10;
              => if %2 { 'L1 } else { 'if_false_11_4 };
          }
          'L0: {
              %1: bool = %b_0.1 == 11;
    ```
  ][
    ```rust
              => if %1 { 'if_true_7_2 } else { 'if_false_7_2 };
          }
          'if_true_11_4: {
              %a_0.6: i32 = 25;
              => 'if_exit_11_4;
          }
          'if_exit_11_4: {
              %a_0.2: i32 = phi('if_exit_13_9: %a_0.3, 'if_true_11_4: %a_0.6);
              => 'if_exit_7_2;
          }
          'if_false_11_4: {
              %4: bool = %b_0.1 == 10;
              => if %4 { 'L2 } else { 'if_false_13_9 };
          }
          'L1: {
              %3: bool = %a_0.1 == 1;
              => if %3 { 'if_true_11_4 } else { 'if_false_11_4 };
          }
          'if_true_13_9: {
              %7: i32 = %a_0.1 + 15;
              %a_0.4: i32 = %7;
              => 'if_exit_13_9;
          }
    ```
  ][
    ```rust
        'if_exit_13_9: {
            %a_0.3: i32 = phi('if_false_13_9: %a_0.5, 'if_true_13_9: %a_0.4);
            => 'if_exit_11_4;
        }
        'if_false_13_9: {
            %8: i32 = %a_0.1;
            %9: i32 = - %8;
            %a_0.5: i32 = %9;
            => 'if_exit_13_9;
        }
        'L2: {
            %5: i32 = - 5;
            %6: bool = %a_0.1 == %5;
            => if %6 { 'if_true_13_9 } else { 'if_false_13_9 };
        }
    }

    fn main() -> i32 {
        'entry: {
            %0: i32 = @ifElseIf();
            %1: () = @print_int(%0);
            return 0;
        }
    }
    ```
  ]
]

---

\#1 Copy Propagation:

#small[
  #grid(columns: 3)[
    ```rust
    fn ifElseIf() -> i32 {
        let a_0: i32;
        let b_0: i32;
        'entry: {
            %a_0.0: i32 = 0;
            %a_0.1: i32 = 5;
            %b_0.0: i32 = 0;
            %b_0.1: i32 = 10;
            %0: bool = 5 == 6;
            => if %0 { 'if_true_7_2 } else { 'L0 };
        }
        'if_true_7_2: {
            return 5;
        }
        'if_exit_7_2: {
            return %a_0.2;
        }
        'if_false_7_2: {
            %2: bool = 10 == 10;
            => if %2 { 'L1 } else { 'if_false_11_4 };
        }
        'L0: {
            %1: bool = 10 == 11;
    ```][
    ```rust
            => if %1 { 'if_true_7_2 } else { 'if_false_7_2 };
        }
        'if_true_11_4: {
            %a_0.6: i32 = 25;
            => 'if_exit_11_4;
        }
        'if_exit_11_4: {
            %a_0.2: i32 = phi('if_exit_13_9: %a_0.3, 'if_true_11_4: 25);
            => 'if_exit_7_2;
        }
        'if_false_11_4: {
            %4: bool = 10 == 10;
            => if %4 { 'L2 } else { 'if_false_13_9 };
        }
        'L1: {
            %3: bool = 5 == 1;
            => if %3 { 'if_true_11_4 } else { 'if_false_11_4 };
        }
        'if_true_13_9: {
            %7: i32 = 5 + 15;
            %a_0.4: i32 = %7;
            => 'if_exit_13_9;
        }
    ```][
    ```rust
        'if_exit_13_9: {
            %a_0.3: i32 = phi('if_false_13_9: %9, 'if_true_13_9: %7);
            => 'if_exit_11_4;
        }
        'if_false_13_9: {
            %8: i32 = 5;
            %9: i32 = - 5;
            %a_0.5: i32 = %9;
            => 'if_exit_13_9;
        }
        'L2: {
            %5: i32 = - 5;
            %6: bool = 5 == %5;
            => if %6 { 'if_true_13_9 } else { 'if_false_13_9 };
        }
    }

    fn main() -> i32 {
        'entry: {
            %0: i32 = @ifElseIf();
            %1: () = @print_int(%0);
            return 0;
        }
    }
    ```
  ]
]

---

\#2 Const Propagation:
#small[
  #grid(columns: 3)[
    ```rust
    fn ifElseIf() -> i32 {
        let a_0: i32;
        let b_0: i32;
        'entry: {
            %a_0.0: i32 = 0;
            %a_0.1: i32 = 5;
            %b_0.0: i32 = 0;
            %b_0.1: i32 = 10;
            %0: bool = 5 == 6;
            => 'L0;
        }
        'if_true_7_2: {
            return 5;
        }
        'if_exit_7_2: {
            return %a_0.2;
        }
        'if_false_7_2: {
            %2: bool = 10 == 10;
            => 'L1;
        }
        'L0: {
            %1: bool = 10 == 11;
    ```][
    ```rust
          => 'if_false_7_2;
      }
      'if_true_11_4: {
          %a_0.6: i32 = 25;
          => 'if_exit_11_4;
      }
      'if_exit_11_4: {
          %a_0.2: i32 = phi('if_exit_13_9: %a_0.3, 'if_true_11_4: 25);
          => 'if_exit_7_2;
      }
      'if_false_11_4: {
          %4: bool = 10 == 10;
          => 'L2;
      }
      'L1: {
          %3: bool = 5 == 1;
          => 'if_false_11_4;
      }
      'if_true_13_9: {
          %7: i32 = 5 + 15;
          %a_0.4: i32 = 20;
          => 'if_exit_13_9;
      }
    ```][
    ```rust
        'if_exit_13_9: {
            %a_0.3: i32 = phi('if_false_13_9: -5, 'if_true_13_9: 20);
            => 'if_exit_11_4;
        }
        'if_false_13_9: {
            %8: i32 = 5;
            %9: i32 = - 5;
            %a_0.5: i32 = -5;
            => 'if_exit_13_9;
        }
        'L2: {
            %5: i32 = - 5;
            %6: bool = 5 == -5;
            => 'if_false_13_9;
        }
    }

    fn main() -> i32 {
        'entry: {
            %0: i32 = @ifElseIf();
            %1: () = @print_int(%0);
            return 0;
        }
    }

    ```
  ]
]

---

\#3 Dead Definition Elimination:

#small[
  #grid(columns: 3)[
    ```rust
    fn ifElseIf() -> i32 {
        let a_0: i32;
        let b_0: i32;
        'entry: {
            => 'L0;
        }
        'if_true_7_2: {
            return 5;
        }
        'if_exit_7_2: {
            return %a_0.2;
        }
        'if_false_7_2: {
            => 'L1;
        }
    ```
  ][
    ```rust
        'L0: {
            => 'if_false_7_2;
        }
        'if_true_11_4: {
            => 'if_exit_11_4;
        }
        'if_exit_11_4: {
            %a_0.2: i32 = phi('if_exit_13_9: %a_0.3, 'if_true_11_4: 25);
            => 'if_exit_7_2;
        }
        'if_false_11_4: {
            => 'L2;
        }
        'L1: {
            => 'if_false_11_4;
        }
        'if_true_13_9: {
            => 'if_exit_13_9;
        }
    ```

  ][
    ```rust
        'if_exit_13_9: {
            %a_0.3: i32 = phi('if_false_13_9: -5, 'if_true_13_9: 20);
            => 'if_exit_11_4;
        }
        'if_false_13_9: {
            => 'if_exit_13_9;
        }
        'L2: {
            => 'if_false_13_9;
        }
    }

    fn main() -> i32 {
        'entry: {
            %0: i32 = @ifElseIf();
            %1: () = @print_int(%0);
            return 0;
        }
    }
    ```
  ]
]

---

\#4 Dead Allocation Elimination:

#small[
  #grid(columns: 3)[
    ```rust
    fn ifElseIf() -> i32 {
        let a_0: i32;
        'entry: {
            => 'L0;
        }
        'if_true_7_2: {
            return 5;
        }
        'if_exit_7_2: {
            return %a_0.2;
        }
        'if_false_7_2: {
            => 'L1;
        }
        'L0: {
            => 'if_false_7_2;
        }
        'if_true_11_4: {
            => 'if_exit_11_4;
        }
    ```
  ][
    ```rust
        'if_exit_11_4: {
            %a_0.2: i32 = phi('if_exit_13_9: %a_0.3, 'if_true_11_4: 25);
            => 'if_exit_7_2;
        }
        'if_false_11_4: {
            => 'L2;
        }
        'L1: {
            => 'if_false_11_4;
        }
        'if_true_13_9: {
            => 'if_exit_13_9;
        }
        'if_exit_13_9: {
            %a_0.3: i32 = phi('if_false_13_9: -5, 'if_true_13_9: 20);
            => 'if_exit_11_4;
        }
    ```
  ][
    ```rust
        'if_false_13_9: {
            => 'if_exit_13_9;
        }
        'L2: {
            => 'if_false_13_9;
        }
    }

    fn main() -> i32 {
        'entry: {
            %0: i32 = @ifElseIf();
            %1: () = @print_int(%0);
            return 0;
        }
    }
    ```
  ]
]
---
\#5 Dead Block Elimination:
#small[
  #grid(columns: 3)[
    ```rust
    fn ifElseIf() -> i32 {
        let a_0: i32;
        'entry: {
            => 'L0;
        }
        'if_exit_7_2: {
            return %a_0.2;
        }
        'if_false_7_2: {
            => 'L1;
        }
        'L0: {
            => 'if_false_7_2;
        }
    ```
  ][
    ```rust
        'if_exit_11_4: {
            %a_0.2: i32 = phi('if_exit_13_9: %a_0.3);
            => 'if_exit_7_2;
        }
        'if_false_11_4: {
            => 'L2;
        }
        'L1: {
            => 'if_false_11_4;
        }
        'if_exit_13_9: {
            %a_0.3: i32 = phi('if_false_13_9: -5);
            => 'if_exit_11_4;
        }
    ```
  ][
    ```rust
        'if_false_13_9: {
            => 'if_exit_13_9;
        }
        'L2: {
            => 'if_false_13_9;
        }
    }

    fn main() -> i32 {
        'entry: {
            %0: i32 = @ifElseIf();
            %1: () = @print_int(%0);
            return 0;
        }
    }
    ```
  ]
]

---
\#6 CFG Simplification:
#small[
  #grid(columns: 3)[
    ```rust
    fn ifElseIf() -> i32 {
        let a_0: i32;
        'entry: {
            => 'if_exit_13_9;
        }
        'if_exit_7_2: {
            %a_0.2: i32 = phi('if_exit_13_9: %a_0.3);
            return %a_0.2;
        }
        'if_false_7_2: {
            => 'if_exit_13_9;
        }
        'L2: {
            => 'if_exit_13_9;
        }
    ```
  ][
    ```rust
        'if_exit_11_4: {
            => 'if_exit_7_2;
        }
        'if_false_11_4: {
            => 'if_exit_13_9;
        }
        'L1: {
            => 'if_exit_13_9;
        }
        'if_exit_13_9: {
            %a_0.3: i32 = phi('L0: -5, 'if_false_11_4: -5, 'L2: -5, 'entry: -5, 'L1: -5, 'if_false_7_2: -5);
            => 'if_exit_7_2;
        }
    ```
  ][
    ```rust
        'if_false_13_9: {
            => 'if_exit_13_9;
        }
        'L0: {
            => 'if_exit_13_9;
        }
    }

    fn main() -> i32 {
        'entry: {
            %0: i32 = @ifElseIf();
            %1: () = @print_int(%0);
            return 0;
        }
    }
    ```
  ]
]
---
\#7 Dead Block Elimination:
#small[
  #grid(columns: 3)[
    ```rust
    fn ifElseIf() -> i32 {
        let a_0: i32;
        'entry: {
            => 'if_exit_13_9;
        }
        'if_exit_7_2: {
            %a_0.2: i32 = phi('if_exit_13_9: %a_0.3);
    ```
  ][
    ```rust
            return %a_0.2;
        }
        'if_exit_13_9: {
            %a_0.3: i32 = phi('entry: -5);
            => 'if_exit_7_2;
        }
    }
    ```
  ][
    ```rust
    fn main() -> i32 {
        'entry: {
            %0: i32 = @ifElseIf();
            %1: () = @print_int(%0);
            return 0;
        }
    }
    ```
  ]
]

\#8 Function Call Inlining:
#small[
  #grid(columns: 3)[
    ```rust
    fn main() -> i32 {
        let inline_ifElseIf_0_a_0: i32;
        'entry: {
            => 'inline_ifElseIf_0_prologue;
        }
        'inline_ifElseIf_0_return: {
            %1: () = @print_int(%0);
            return 0;
        }
    ```
  ][
    ```rust
        'inline_ifElseIf_0_prologue: {
            => 'inline_ifElseIf_0_entry;
        }
        'inline_ifElseIf_0_entry: {
            => 'inline_ifElseIf_0_if_exit_13_9;
        }
        'inline_ifElseIf_0_if_exit_7_2: {
            %inline_ifElseIf_0_a_0.2: i32 = phi('inline_ifElseIf_0_if_exit_13_9: %inline_ifElseIf_0_a_0.3);
            => 'inline_ifElseIf_0_epilogue;
        }
    ```
  ][
    ```rust
        'inline_ifElseIf_0_if_exit_13_9: {
            %inline_ifElseIf_0_a_0.3: i32 = phi('inline_ifElseIf_0_entry: -5);
            => 'inline_ifElseIf_0_if_exit_7_2;
        }
        'inline_ifElseIf_0_epilogue: {
            %0: i32 = phi('inline_ifElseIf_0_if_exit_7_2: %inline_ifElseIf_0_a_0.2);
            => 'inline_ifElseIf_0_return;
        }
    }
    ```
  ]
]
---
\#9 Copy Propagation:
#small[
  #grid(columns: 3)[
    ```rust
    fn main() -> i32 {
        let inline_ifElseIf_0_a_0: i32;
        'entry: {
            => 'inline_ifElseIf_0_prologue;
        }
        'inline_ifElseIf_0_return: {
            %1: () = @print_int(%inline_ifElseIf_0_a_0.3);
            return 0;
        }
    ```
  ][
    ```rust
        'inline_ifElseIf_0_prologue: {
            => 'inline_ifElseIf_0_entry;
        }
        'inline_ifElseIf_0_entry: {
            => 'inline_ifElseIf_0_if_exit_13_9;
        }
        'inline_ifElseIf_0_if_exit_7_2: {
            %inline_ifElseIf_0_a_0.2: i32 = phi('inline_ifElseIf_0_if_exit_13_9: %inline_ifElseIf_0_a_0.3);
            => 'inline_ifElseIf_0_epilogue;
        }
    ```
  ][
    ```rust
        'inline_ifElseIf_0_if_exit_13_9: {
            %inline_ifElseIf_0_a_0.3: i32 = phi('inline_ifElseIf_0_entry: -5);
            => 'inline_ifElseIf_0_if_exit_7_2;
        }
        'inline_ifElseIf_0_epilogue: {
            %0: i32 = phi('inline_ifElseIf_0_if_exit_7_2: %inline_ifElseIf_0_a_0.3);
            => 'inline_ifElseIf_0_return;
        }
    }
    ```
  ]
]

\#10 Const Propagation:
#small[
  #grid(columns: 3)[
    ```rust
    fn main() -> i32 {
        let inline_ifElseIf_0_a_0: i32;
        'entry: {
            => 'inline_ifElseIf_0_prologue;
        }
        'inline_ifElseIf_0_return: {
            %1: () = @print_int(-5);
            return 0;
        }
    ```
  ][
    ```rust
        'inline_ifElseIf_0_prologue: {
            => 'inline_ifElseIf_0_entry;
        }
        'inline_ifElseIf_0_entry: {
            => 'inline_ifElseIf_0_if_exit_13_9;
        }
        'inline_ifElseIf_0_if_exit_7_2: {
            %inline_ifElseIf_0_a_0.2: i32 = phi('inline_ifElseIf_0_if_exit_13_9: -5);
            => 'inline_ifElseIf_0_epilogue;
        }
    ```
  ][
    ```rust
        'inline_ifElseIf_0_if_exit_13_9: {
            %inline_ifElseIf_0_a_0.3: i32 = phi('inline_ifElseIf_0_entry: -5);
            => 'inline_ifElseIf_0_if_exit_7_2;
        }
        'inline_ifElseIf_0_epilogue: {
            %0: i32 = phi('inline_ifElseIf_0_if_exit_7_2: -5);
            => 'inline_ifElseIf_0_return;
        }
    }
    ```
  ]
]
---
\#11 Dead Definition Elimination:
#small[
  #grid(columns: 3)[
    ```rust
    fn main() -> i32 {
        let inline_ifElseIf_0_a_0: i32;
        'entry: {
            => 'inline_ifElseIf_0_prologue;
        }
        'inline_ifElseIf_0_return: {
            %1: () = @print_int(-5);
            return 0;
        }
    ```
  ][
    ```rust
        'inline_ifElseIf_0_prologue: {
            => 'inline_ifElseIf_0_entry;
        }
        'inline_ifElseIf_0_entry: {
            => 'inline_ifElseIf_0_if_exit_13_9;
        }
        'inline_ifElseIf_0_if_exit_7_2: {
            => 'inline_ifElseIf_0_epilogue;
        }
    ```
  ][
    ```rust
        'inline_ifElseIf_0_if_exit_13_9: {
            => 'inline_ifElseIf_0_if_exit_7_2;
        }
        'inline_ifElseIf_0_epilogue: {
            => 'inline_ifElseIf_0_return;
        }
    }
    ```
  ]
]

\#12 Dead Allocation Elimination:
#small[
  #grid(columns: 3)[
    ```rust
    fn main() -> i32 {
        'entry: {
            => 'inline_ifElseIf_0_prologue;
        }
        'inline_ifElseIf_0_return: {
            %1: () = @print_int(-5);
            return 0;
        }
    ```
  ][
    ```rust
        'inline_ifElseIf_0_prologue: {
            => 'inline_ifElseIf_0_entry;
        }
        'inline_ifElseIf_0_entry: {
            => 'inline_ifElseIf_0_if_exit_13_9;
        }
        'inline_ifElseIf_0_if_exit_7_2: {
            => 'inline_ifElseIf_0_epilogue;
        }
    ```
  ][
    ```rust
        'inline_ifElseIf_0_if_exit_13_9: {
            => 'inline_ifElseIf_0_if_exit_7_2;
        }
        'inline_ifElseIf_0_epilogue: {
            => 'inline_ifElseIf_0_return;
        }
    }
    ```
  ]
]
---
\#13 CFG Simplification:
#small[
  #grid(columns: 3)[
    ```rust
    fn main() -> i32 {
        'entry: {
            => 'inline_ifElseIf_0_return;
        }
        'inline_ifElseIf_0_return: {
            %1: () = @print_int(-5);
            return 0;
        }
    ```
  ][
    ```rust
        'inline_ifElseIf_0_prologue: {
            => 'inline_ifElseIf_0_return;
        }
        'inline_ifElseIf_0_entry: {
            => 'inline_ifElseIf_0_return;
        }
        'inline_ifElseIf_0_if_exit_7_2: {
            => 'inline_ifElseIf_0_return;
        }
    ```
  ][
    ```rust
        'inline_ifElseIf_0_if_exit_13_9: {
            => 'inline_ifElseIf_0_return;
        }
        'inline_ifElseIf_0_epilogue: {
            => 'inline_ifElseIf_0_return;
        }
    }
    ```
  ]
]

\#14 Dead Block Elimination:
#small[
  ```rust
  fn main() -> i32 {
      'entry: {
          => 'inline_ifElseIf_0_return;
      }
      'inline_ifElseIf_0_return: {
          %1: () = @print_int(-5);
          return 0;
      }
  }
  ```
]
---
\#15 CFG Simplification:
#small[
  ```rust
  fn main() -> i32 {
      'entry: {
          %1: () = @print_int(-5);
          return 0;
      }
      'inline_ifElseIf_0_return: {
          => 'entry;
      }
  }

  ```
]

\#16 Dead Block Elimination:
```rust
fn main() -> i32 {
    'entry: {
        %1: () = @print_int(-5);
        return 0;
    }
}
```
