# IR 解释执行测试说明

本实验选取了少数几个测试用例，用于验证编译器前端产生 IR 以及解释执行的正确性。

## 测试用例功能说明

| 文件名          | 功能描述        | 输入   | 输出             | 返回值 |
| --------------- | --------------- | ------ | ---------------- | ------ |
| 1-return.c      | `main()` 返回值 | 无     | 无               | 83     |
| 8-controlflow.c | 复杂控制流      | 无     | 无               | 0      |
| 11-comp.c       | 穷举完全数      | 正整数 | 限制内的完全数   | 0      |
| 12-intpi.c      | 积分算圆周率    | 正整数 | 指定步数的近似值 | 0      |

## 测试流程参考

对于每一个测试用例，使用自己的编译器前端生成 IR，并且交给模拟器进行解释执行，得到输出和返回值。

对于同一个测试用例，制作副本并在其文件开头插入下面的内容：

```c
#include <stdio.h>
#include <stdbool.h>

#ifndef SCOPE
#define SCOPE static
#endif

SCOPE void print_int(int d)
{
	printf("%d\n", d);
}
SCOPE void print_double(double x)
{
	printf("%g\n", x);
}
SCOPE void print_float(float x)
{
	print_double(x);
}
SCOPE void print_bool(bool b)
{
	if (b) puts("true");
	else puts("false");
}
SCOPE int get_int(void)
{
	int d = 0;
	scanf("%d", &d);
	return d;
}
SCOPE float get_float(void)
{
	float x = 0.0f;
	scanf("%f", &x);
	return x;
}
SCOPE double get_double(void)
{
	double x = 0.0;
	scanf("%lf", &x);
	return x;
}

```

然后调用 GCC 进行编译并运行，例如：

```bash
$ gcc 1-return-copy.c -o 1-return
$ ./1-return
```

然后用键盘提供相同的输入（如果该测试用例需要），比较其输出和返回值（用 `echo $?` 读取）是否与模拟器的结果相同。





