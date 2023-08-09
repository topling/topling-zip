#pragma once
#include <time.h>
#include <terark/config.hpp>

namespace terark {

// with params time zone and dst
TERARK_DLL_EXPORT void nolocks_localtime_tzd(struct tm*, time_t, long tz, int dst);

// with params time zone
TERARK_DLL_EXPORT void nolocks_localtime_tz(struct tm*, time_t, long tz);

// proto type is same as localtime
TERARK_DLL_EXPORT struct tm* nolocks_localtime(const time_t*);

// proto type is same as localtime_r
TERARK_DLL_EXPORT struct tm* nolocks_localtime_r(const time_t*, struct tm*);

} // namespace terark
