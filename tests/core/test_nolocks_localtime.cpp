#include <terark/util/nolocks_localtime.hpp>
#include <terark/util/function.hpp>
#include <stdio.h>

using namespace terark;

void test(time_t t) {
    auto& tm1 = *localtime(&t);
    auto& tm2 = *nolocks_localtime(&t);
    TERARK_VERIFY_EQ(tm1.tm_sec , tm2.tm_sec );
    TERARK_VERIFY_EQ(tm1.tm_min , tm2.tm_min );
    TERARK_VERIFY_EQ(tm1.tm_hour, tm2.tm_hour);
    TERARK_VERIFY_EQ(tm1.tm_mday, tm2.tm_mday);
    TERARK_VERIFY_EQ(tm1.tm_wday, tm2.tm_wday);
    TERARK_VERIFY_EQ(tm1.tm_yday, tm2.tm_yday);
    TERARK_VERIFY_EQ(tm1.tm_year, tm2.tm_year);
    TERARK_VERIFY_EQ(tm1.tm_mon , tm2.tm_mon );
    TERARK_VERIFY_EQ(tm1.tm_zone, tm2.tm_zone);
    TERARK_VERIFY_EQ(tm1.tm_gmtoff, tm2.tm_gmtoff);
}
int main() {
    printf("nolocks_localtime: passed\n");
    time_t day_sec = 86400;
    test(0); // epoch
    test(day_sec * ( 1*365));
    test(day_sec * ( 2*365 + 31 + 29 +  0)); // 1972-02-29
    test(day_sec * ( 3*365 + 31 + 28 +  1)); // 1973-02-28
    test(day_sec * (26*365 + 31 + 29 +  6)); // 1996-02-29, leap year
    test(day_sec * (30*365 + 31 + 29 +  7)); // 2000-02-29, leap year
    test(day_sec * (34*365 + 31 + 29 +  8)); // 2004-02-29, leap year
    test(day_sec * (52*365 + 31 + 28 + 12)); // 2022-02-28
    test(day_sec * (52*365 + 31 + 28 + 12) + 23459); // 2022-02-28
    return 0;
}
