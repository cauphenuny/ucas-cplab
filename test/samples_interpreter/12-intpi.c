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
double get_pi(double n)
{
	double i = 1.0, s = 0.0;
	double x;
	while (i <= n)
	{
		x = i / n;
		s = s + 1.0 / (1.0 + x * x);
		i = i + 1.0;
	}
	return 4.0 * s / n;
}

int main()
{
	double n, pi;
	n = get_double();
	if (n > 0.0)
	{
		pi = get_pi(n);
		print_double(pi);
		return 0;
	}
	else
		return 1;
}
