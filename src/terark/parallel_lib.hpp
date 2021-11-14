#pragma once

#if defined(TERARK_ENABLE_PARALLEL) && defined(__GNUC__) && __GNUC__ * 1000 + __GNUC_MINOR__ >= 4007
	#include <parallel/algorithm>
	#define terark_parallel_sort __gnu_parallel::sort
#else
	#include <algorithm>
	#define terark_parallel_sort std::sort
#endif

