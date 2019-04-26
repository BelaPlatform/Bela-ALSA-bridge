template <typename T1, typename T2>
static bool areEqual(const T1& vec1, const T2& vec2)
{
	if(vec1.size() != vec2.size())
		return false;	
	for(unsigned int n = 0; n < vec1.size(); ++n)
	{
		if(vec1[n] != vec2[n])
		{
			return false;
		}
	}
	return true;
}

template <typename T>
static void scramble(T& vec)
{
	for(unsigned int n = 0; n < vec.size(); ++n)
	{
		vec[n] = rand();
	}
}

