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

