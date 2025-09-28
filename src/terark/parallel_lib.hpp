#pragma once

#if defined(TOPLING_ENABLE_PARALLEL_ALGO) && defined(__GNUC__) && __GNUC__ * 1000 + __GNUC_MINOR__ >= 4007
	#include <parallel/algorithm>
	#define terark_parallel_sort __gnu_parallel::sort
#else
	#include <algorithm>
	#define terark_parallel_sort std::sort
#endif

