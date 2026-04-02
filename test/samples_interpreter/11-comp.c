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
int main()
{
	int end;
	end = get_int();	// limit
	if (end <= 0)
		return -1;
	int i = 1, d, s;
	while (i <= end)
	{
		d = 1;
		s = 0;
		while (d < i)
		{
			if (i % d == 0)
				s = s + d;
			d = d + 1;
		}
		if (s == i)
			print_int(i); 
		i = i + 1;
	}
	return 0;
}

