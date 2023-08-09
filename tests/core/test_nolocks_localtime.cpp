#include <terark/util/nolocks_localtime.hpp>
#include <terark/util/function.hpp>
#include <stdio.h>

namespace redis {
static int is_leap_year(time_t year) {
    if (year % 4) return 0;         /* A year not divisible by 4 is not leap. */
    else if (year % 100) return 1;  /* If div by 4 and not 100 is surely leap. */
    else if (year % 400) return 0;  /* If div by 100 *and* 400 is not leap. */
    else return 1;                  /* If div by 100 and not by 400 is leap. */
}

static int g_daylight_active = [] {
  tzset(); // Now 'timezome' global is populated.
  time_t t = time(NULL);
  struct tm *aux = localtime(&t); // safe in global cons
  return aux->tm_isdst;
}();

void nolocks_localtime(struct tm *tmp, time_t t, time_t tz, int dst) {
    const time_t secs_min = 60;
    const time_t secs_hour = 3600;
    const time_t secs_day = 3600*24;

    t -= tz;                            /* Adjust for timezone. */
    t += 3600*dst;                      /* Adjust for daylight time. */
    time_t days = t / secs_day;         /* Days passed since epoch. */
    time_t seconds = t % secs_day;      /* Remaining seconds. */

    tmp->tm_isdst = dst;
    tmp->tm_hour = seconds / secs_hour;
    tmp->tm_min = (seconds % secs_hour) / secs_min;
    tmp->tm_sec = (seconds % secs_hour) % secs_min;

    /* 1/1/1970 was a Thursday, that is, day 4 from the POV of the tm structure
     * where sunday = 0, so to calculate the day of the week we have to add 4
     * and take the modulo by 7. */
    tmp->tm_wday = (days+4)%7;

    /* Calculate the current year. */
    int year = 1970;
    while(1) {
        /* Leap years have one day more. */
        time_t days_this_year = 365 + is_leap_year(year);
        if (days_this_year > days) break;
        days -= days_this_year;
        year++;
    }
    tmp->tm_yday = days;  /* Number of day of the current year. */
    /* We need to calculate in which month and day of the month we are. To do
     * so we need to skip days according to how many days there are in each
     * month, and adjust for the leap year that has one more day in February. */
    unsigned char mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    mdays[1] += is_leap_year(year);

    int mon = 0;
    while(days >= mdays[mon]) {
        days -= mdays[mon];
        mon++;
    }
    tmp->tm_mon = mon;

    tmp->tm_mday = days+1;  /* Add 1 since our 'days' is zero-based. */
    tmp->tm_year = year - 1900; /* Surprisingly tm_year is year-1900. */
}

void nolocks_localtime(struct tm *tmp, time_t t, time_t tz) {
  return nolocks_localtime(tmp, t, tz, g_daylight_active);
}

void nolocks_localtime(struct tm *tmp, time_t t) {
  return nolocks_localtime(tmp, t, timezone, g_daylight_active);
}

struct tm* LocalTime(const time_t* t) {
  thread_local struct tm tmp;
  nolocks_localtime(&tmp, *t, timezone, g_daylight_active);
  return &tmp;
}

struct tm* LocalTimeR(const time_t* timep, struct tm* result) {
  nolocks_localtime(result, *timep);
  return result;
}

} // namespace redis

using namespace terark;

static struct tm* baseline_localtime(const time_t* t) {
    static const bool USE_SYS = atoi(getenv("USE_SYS")?:"0");
    if (USE_SYS) {
        return localtime(t);
    } else {
        return redis::LocalTime(t);
    }
}
void test(time_t t) {
    auto tm1 = *baseline_localtime(&t);
    auto tm2 = *nolocks_localtime(&t);
    TERARK_VERIFY_EQ(tm1.tm_isdst, tm2.tm_isdst);
    TERARK_VERIFY_EQ(tm1.tm_sec , tm2.tm_sec );
    TERARK_VERIFY_EQ(tm1.tm_min , tm2.tm_min );
    TERARK_VERIFY_EQ(tm1.tm_hour, tm2.tm_hour);
    TERARK_VERIFY_EQ(tm1.tm_mday, tm2.tm_mday);
    TERARK_VERIFY_EQ(tm1.tm_wday, tm2.tm_wday);
    TERARK_VERIFY_EQ(tm1.tm_yday, tm2.tm_yday);
    TERARK_VERIFY_EQ(tm1.tm_year, tm2.tm_year);
    TERARK_VERIFY_EQ(tm1.tm_mon , tm2.tm_mon );
    //TERARK_VERIFY_EQ(tm1.tm_zone, tm2.tm_zone);
    //TERARK_VERIFY_EQ(tm1.tm_gmtoff, tm2.tm_gmtoff);
}
int main() {
    time_t day_sec = 86400;
    test(0); // epoch
    test(timezone); // epoch
    test(day_sec * ( 1*365));
    test(day_sec * ( 2*365 + 31 + 29 +  0)); // 1972-02-29
    test(day_sec * ( 2*365 +  0 +  0 +  0) + 23459); // 1972-01-01
    test(day_sec * ( 3*365 + 31 + 28 +  1)); // 1973-02-28
    test(day_sec * (26*365 + 31 + 29 +  6)); // 1996-02-29, leap year
    test(day_sec * (30*365 + 31 + 29 +  7)); // 2000-02-29, leap year
    test(day_sec * (34*365 + 31 + 29 +  8)); // 2004-02-29, leap year
    test(day_sec * (52*365 + 31 + 28 + 12)); // 2022-02-28
    test(day_sec * (52*365 + 31 + 28 + 12) + 23459); // 2022-02-28
    for (unsigned day = 0; day < 365*67; day++) {
        test(day_sec * day + day);
    }
    printf("nolocks_localtime: passed\n");
    return 0;
}
