#include <terark/bitmanip.hpp>
#include <stdint.h>
#include <time.h>
#include "nolocks_localtime.hpp"

namespace terark {

// 400 years are suffient, but needs (year % 400) which is slow,
// 4000 bits are just 500 bytes, and hot area is just one uint64
static const uint64_t leap_bits[] = { // year [0000, 4000)
  0x1111111111111111, 0x1111110111111111, 0x1111111111111111, 0x1111111111111011, 0x1111011111111111,
  0x1111111111111111, 0x1111111111111111, 0x1101111111111111, 0x1111111111111111, 0x1111111110111111,
  0x0111111111111111, 0x1111111111111111, 0x1111111111111111, 0x1111111111111111, 0x1111111111111101,
  0x1111101111111111, 0x1111111111111111, 0x1111111111110111, 0x1111111111111111, 0x1111111111111111,
  0x1111111111011111, 0x1011111111111111, 0x1111111111111111, 0x1111111101111111, 0x1111111111111111,
  0x1111111111111111, 0x1111110111111111, 0x1111111111111111, 0x1111111111111011, 0x1111011111111111,
  0x1111111111111111, 0x1111111111111111, 0x1101111111111111, 0x1111111111111111, 0x1111111110111111,
  0x0111111111111111, 0x1111111111111111, 0x1111111111111111, 0x1111111111111111, 0x1111111111111101,
  0x1111101111111111, 0x1111111111111111, 0x1111111111110111, 0x1111111111111111, 0x1111111111111111,
  0x1111111111011111, 0x1011111111111111, 0x1111111111111111, 0x1111111101111111, 0x1111111111111111,
  0x1111111111111111, 0x1111110111111111, 0x1111111111111111, 0x1111111111111011, 0x1111011111111111,
  0x1111111111111111, 0x1111111111111111, 0x1101111111111111, 0x1111111111111111, 0x1111111110111111,
  0x0111111111111111, 0x1111111111111111, 0x1111111111111111, 0x1111111111111111, 0x1111111111111101,
};
static const uint16_t leap_rank1[] = { // year [0000, 4000)
  0x0000,
  0x0010, 0x001F, 0x002F, 0x003E, 0x004D,
  0x005D, 0x006D, 0x007C, 0x008C, 0x009B,
  0x00AA, 0x00BA, 0x00CA, 0x00DA, 0x00E9,
  0x00F8, 0x0108, 0x0117, 0x0127, 0x0137,
  0x0146, 0x0155, 0x0165, 0x0174, 0x0184,
  0x0194, 0x01A3, 0x01B3, 0x01C2, 0x01D1,
  0x01E1, 0x01F1, 0x0200, 0x0210, 0x021F,
  0x022E, 0x023E, 0x024E, 0x025E, 0x026D,
  0x027C, 0x028C, 0x029B, 0x02AB, 0x02BB,
  0x02CA, 0x02D9, 0x02E9, 0x02F8, 0x0308,
  0x0318, 0x0327, 0x0337, 0x0346, 0x0355,
  0x0365, 0x0375, 0x0384, 0x0394, 0x03A3,
  0x03B2, 0x03C2, 0x03D2, 0x03E2, 0x03F1,
};

static bool is_leap_year_fast(time_t year) {
  return terark::terark_bit_test(leap_bits, year);
  //constexpr auto wb = sizeof(leap_bits[0]) * 8;
  //return (leap_bits[year / wb] & (uint64_t(1) << (year % wb))) != 0;
}

static bool is_leap_year_slow(time_t year) {
    if (year % 4) return 0;         /* A year not divisible by 4 is not leap. */
    else if (year % 100) return 1;  /* If div by 4 and not 100 is surely leap. */
    else if (year % 400) return 0;  /* If div by 100 *and* 400 is not leap. */
    else return 1;                  /* If div by 100 and not by 400 is leap. */
}

static bool is_leap_year(time_t year) {
  if (terark_likely(year >= 0 && year < 4000))
    return is_leap_year_fast(year);
  else
    return is_leap_year_slow(year);
}

static const char* g_tzname = nullptr;
static long g_tm_gmtoff = 0;

static int g_daylight_active = [] {
  tzset(); // Now 'timezone' global is populated.
  time_t t = time(NULL);
  struct tm *aux = localtime(&t); // safe in global cons
  g_tzname = aux->tm_zone;
  g_tm_gmtoff = aux->tm_gmtoff;
  return aux->tm_isdst;
}();

#if 0
static long long calc_year_of_day_slow(long long& day) {
  long long year = 1970;
  long long day2 = day; // copy to register
  while (true) {
    time_t days_this_year = 365 + is_leap_year(year);
    if (days_this_year > day2)
      break;
    day2 -= days_this_year;
    year++;
  }
  day = day2;
  return year;
}
#endif

static long long epoch_day_of_year(long long year) {
  auto base = leap_rank1[year / 64];
  auto tail = terark::fast_popcount_trail(leap_bits[year / 64], year % 64);
  return base + tail + 365 * year;
}

static const long long epoch_day_of_1970 = epoch_day_of_year(1970);

static long long calc_year_of_day_fast(long long& day) {
  day += epoch_day_of_1970;
  const auto days_per_400_years = 365 * 400 + 100 - 3;
  auto year = day * 400 / days_per_400_years;
  auto day1 = epoch_day_of_year(year);
  if (day1 < day) {
    long long day2;
    while ((day2 = epoch_day_of_year(year + 1)) < day)
      day1 = day2, year++;
  }
  else if (year > 0) {
    long long day2;
    while ((day2 = epoch_day_of_year(year - 1)) > day)
      day1 = day2, year--;
  }
  else { // year == 0, is leap
    assert(is_leap_year(year));
    assert(day <= 365);
    assert(day1 == 0);
    // do nothing
  }
  day -= day1;
  return year;
}

static int month_day(long long year, long long& day_of_year) {
  static const unsigned char norm_mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  static const unsigned char leap_mdays[12] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  auto mdays = is_leap_year(year) ? leap_mdays : norm_mdays;
  int month = 0;
  auto days = day_of_year; // copy to register
  while(days >= mdays[month]) {
      days -= mdays[month];
      month++;
  }
  day_of_year = days; // now it is day of the month
  return month;
}

TERARK_DLL_EXPORT
void nolocks_localtime_tzd(struct tm *p_tm, time_t t, long tz, int dst) {
  const time_t secs_min = 60;
  const time_t secs_hour = 3600;
  const time_t secs_day = 3600*24;

  t -= tz;                        // Adjust for timezone.
  t += 3600*dst;                  // Adjust for daylight time.
  long long days = t / secs_day;  // Days passed since epoch.
  time_t seconds = t % secs_day;  // Remaining seconds.

  p_tm->tm_isdst = dst;
  p_tm->tm_hour = seconds / secs_hour;
  p_tm->tm_min = (seconds % secs_hour) / secs_min;
  p_tm->tm_sec = (seconds % secs_hour) % secs_min;

  // 1/1/1970 was a Thursday, that is, day 4 from the POV of the tm structure
  // where sunday = 0, so to calculate the day of the week we have to add 4
  // and take the modulo by 7.
  p_tm->tm_wday = (days+4)%7;

  int year = (int)calc_year_of_day_fast(days);
  p_tm->tm_yday = (int)days;  // Number of day of the current year.
  p_tm->tm_mon = month_day(year, days);

  p_tm->tm_mday = (int)days+1;  // Add 1 since our 'days' is zero-based.
  p_tm->tm_year = year - 1900;  // Surprisingly tm_year is year-1900.
  p_tm->tm_zone = g_tzname;
  p_tm->tm_gmtoff = g_tm_gmtoff;
}

TERARK_DLL_EXPORT
void nolocks_localtime_tz(struct tm *p_tm, time_t t, time_t tz) {
  return nolocks_localtime_tzd(p_tm, t, tz, g_daylight_active);
}

TERARK_DLL_EXPORT
struct tm* nolocks_localtime(const time_t* t) {
  thread_local struct tm tm_tls;
  nolocks_localtime_tzd(&tm_tls, *t, timezone, g_daylight_active);
  return &tm_tls;
}

TERARK_DLL_EXPORT
struct tm* nolocks_localtime_r(const time_t* t, struct tm* result) {
  nolocks_localtime_tzd(result, *t, timezone, g_daylight_active);
  return result;
}

} // namespace terark
