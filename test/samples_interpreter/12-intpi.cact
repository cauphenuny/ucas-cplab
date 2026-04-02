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
