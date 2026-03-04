# CACT 实验框架

最近更新：2026 年 3 月 4 日。

## 目录结构

deps: ANTLR 工具及其 C++ 库，不要修改。

grammar: 语法文件（.g4）以及生成的语法分析器，**不要修改除 .g4 文件之外的文件**。

src: CACT 编译器相关的源文件，建议同学们将自己编写的代码置于此目录。

test: 各个层次的测试程序。对于 PR1 和 PR2 而言，每个程序前两位数字是编号，紧随其后的“true”“false”标志代表该程序是否有语法、语义上的错误。

## 构建运行步骤

### 使用 ANTLR 生成语法分析器

推荐使用 C++ 语言，Visitor 模式。

```bash
$ cd grammar
$ java -jar ../deps/antlr-4.13.1-complete.jar -Dlanguage=Cpp CACT.g4 -visitor -no-listener
```

### 构建整个项目

```bash
$ mkdir -p build
$ cd build
$ cmake ..
$ make -j
```

### 运行测试（PR1）

对某个单独的测试程序运行：

```bash
$ ./compiler ../test/samples_lex_and_syntax/00_true_main.cact
```

使用脚本对所有词法语法测试程序（"samples_lex_and_syntax"）运行：

```bash
$ cd test
$ bash test_syntax.sh
```

测试过程中，脚本依次调用编译器对每个测试程序运行，输出绿色的“True!”代表编译器认为该程序合法（返回零值），输出红色的“False!”代表编译器发现了其中的语法错误（返回非零值）。若编译器把语法正确的程序（例如“00_true_main.cact”）判定为错误，或未能对语法错误的程序进行报错，则脚本输出白色的“\*\*Error!”表示该测试未能通过。

### 运行测试（PR2）

语义检查部分与 PR1 类似，执行脚本 "test_semantic.sh" 即可：

```bash
$ cd test
$ bash test_semantic.sh
```

解释执行部分，可参考测试样例 "test/samples_interpreter" 及其中的说明文档。

### 运行测试（PR3）

实验 3 测试流程较复杂，请大家根据下面的参考步骤耐心进行，如果遇到问题请与助教联系。

#### 获取测试程序

从国科大在线网站下载“test_project3.zip”压缩包，将其内容全部解压到 CACT 实验仓库的“test”目录下（直接放入，不要带多余的一层目录“test_project3”），然后将 CACTIO 库文件“libcactio.a”（提供了 `print_int()` 等内置函数的实现）移动到仓库的“build”目录，其它文件保持在“test”目录即可。

确保 "build" 目录下有 "libcactio.a"（CACTIO 库文件，提供了 `print_int()` 等内置函数的实现），"test" 目录下有 "cactio.c" 和 "libcactio.c" 这两个文件（对应的源代码）。

提供的 "libcactio.a" 是已经编译好的，如果在自己机器上不能正常使用，可以考虑根据提供的源代码 "libcactio.c" 手动生成一个：

（设当前位于 "test" 目录下）

```bash
$ riscv64-unknown-elf-gcc libcactio.c -c -o ../build/cactio.o
$ riscv64-unknown-elf-ar rcs ../build/libcactio.a ../build/cactio.o
```

#### 测试 CACT 编译器

使用自己的编译器将一个 CACT 源码文件（设为 "test.cact"）编译并运行的参考流程为：

首先编译为汇编文件，设为 "test.s"：

```bash
$ ./compiler ./test.cact
```

接下来使用 RV64 的 GCC 工具链完成汇编和链接过程，需要准备 CACTIO 库 "libcactio.a"，设其置于路径 "../build" 下，则可用下面的命令来汇编 "test.s" 并链接 "libcactio.a"，得到 RV64 的可执行文件 "test"：

```bash
$ riscv64-unknown-elf-gcc test.s -L../build -lcactio -o test
```

最后就可以使用 Spike 模拟运行该可执行文件了：

```bash
$ spike pk ./test
```

此外，使用 `-s` 选项可以统计程序执行的总指令数：

```bash
$ spike pk -s ./test
```

进行编译器功能测试时，可参考使用自动脚本 "test_functional-g.py"（在 "samples_codegen_functional" 目录中），按照文件开头的提示修改开头的几个变量定义后执行：

```bash
$ python3 test_functional-g.py
```

即可看到对每个 "samples_codegen_functional" 的测试样例编译器是否运行正常。该脚本将自动完成（使用 CACT 编译器）编译、（使用 GCC）汇编和链接、（使用 Spike）运行的过程，并且把程序的输出和返回值（重定向到 "temp" 目录的特定文件）与参考输出和返回值（"samples_codegen_functional" 目录里 ".out" 后缀的文件）进行对比。（注：由于系统环境原因，参考输出里的返回值实际上是程序的返回值做了截断，可以理解为 `int` 强制转换为 `unsigned char`）。

若要进行性能测试，或者是具体检查某一个测试样例的运行效果，请按照下一部分的步骤进行。

#### 对比 GCC

在与 GCC 对比时，除了进行上面的使用 CACT 编译器进行编译、使用 GCC 工具链进行链接、使用 Spike 进行模拟之外，还要将同一个 CACT 源文件经处理（具体来说就是在文件前面添加 "cactio.c" 中定义内置函数的内容）后变成合法的 C 语言文件再给 GCC 去编译（类似于实验 2 中与 GCC 对比测试模拟器的步骤）。这部分可参考提供的脚本 "cact-autotest.sh"，使用说明如下：

确认开头的目录变量定义符合当前环境：

```bash
LIBCACTIO_PATH=../build
TEST_PATH=../test
TEMP_PATH=../temp
CACT_COMPILER=../build/compiler
SPIKE=/opt/riscv/bin/spike
RVCC=/opt/riscv/bin/riscv64-unknown-elf-gcc
```

注意：该脚本默认 CACT 编译器支持了 `-o` 选项指定输出的汇编文件，以将汇编文件指定为源文件名加 ""-ct.s" 后缀。如果同学们的编译器未支持该选项，可能需要根据自己的情况修改 `asmfile` 变量的定义，以确保在使用 `RVCC`（RISC-V GCC）进行汇编和链接时能找到它。

然后即可执行：

```bash
$ bash cact-autotest.sh <CACT source file> <options>
```

其中，`<CACT source file>` 指定 CACT 源文件，`<options>` 指定传递给 CACT 编译器的额外选项（例如 `-O1` 优化等）。

执行后，如果编译都正常，则可以看到输出：

```bash
INFO: Compiling CACT file to RV64 ASM...
INFO: Assembling and linking RV64 ASM file to ELF...
INFO: Generating C code...
INFO: Compiling C file with gcc...
INFO: Press Enter to start the ELF file from CACT compiler... 
> 
```

此时根据提示，按下回车即可启动由 CACT 编译器生成的程序。执行完毕后，可以看到程序的输出以及 Spike 给出的动态指令数（用来近似表示性能）。并且会出现新的提示：

```bash
INFO: Press Enter to start the ELF file from gcc... 
> 
```

此时再次按下回车，即可启动由 GCC 编译生成的程序进行执行效果对比。如果不需要，则直接 Ctrl+C 退出即可。

