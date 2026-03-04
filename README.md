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

使用脚本对所有词法语法测试程序（“samples_lex_and_syntax”）运行：

```bash
$ cd test
$ bash test_syntax.sh
```

测试过程中，脚本依次调用编译器对每个测试程序运行，输出绿色的“True!”代表编译器认为该程序合法（返回零值），输出红色的“False!”代表编译器发现了其中的语法错误（返回非零值）。若编译器把语法正确的程序（例如“00_true_main.cact”）判定为错误，或未能对语法错误的程序进行报错，则脚本输出白色的“\*\*Error!”表示该测试未能通过。

