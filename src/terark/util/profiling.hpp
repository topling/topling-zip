#pragma once

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
# pragma warning(disable: 4127)
#endif

#include "../config.hpp"
#include <terark/stdtypes.hpp>

#include <stdio.h> // TERARK_VERIFY_F
#include <stdlib.h> // for abort() in TERARK_VERIFY_F
#if !defined(_MSC_VER)
#  include <time.h>
#  include <sys/time.h>
#endif

namespace terark {

	class TERARK_DLL_EXPORT profiling
	{
#if defined(_MSC_VER)
		long long m_freq;
#endif

	public:
		profiling();

		long long now() const;

#if defined(_MSC_VER)
		long long ns(long long x) const	{ return x * 1000 / (m_freq / 1000000); }
		long long us(long long x) const	{ return x * 1000 / (m_freq / 1000); }
		long long ms(long long x) const	{ return x * 1000 / (m_freq); }

		long long ns(long long x, long long y) const { return (y-x) * 1000 / (m_freq / 1000000); }
		long long us(long long x, long long y) const { return (y-x) * 1000 / (m_freq / 1000); }
		long long ms(long long x, long long y) const { return (y-x) * 1000 / (m_freq); }

		double nf(long long x) const	{ return x*1e9/m_freq; }
		double uf(long long x) const	{ return x*1e6/m_freq; }
		double mf(long long x) const	{ return x*1e3/m_freq; }

		double nf(long long x, long long y) const { return (y-x)*1e9 / m_freq; }
		double uf(long long x, long long y) const { return (y-x)*1e6 / m_freq; }
		double mf(long long x, long long y) const { return (y-x)*1e3 / m_freq; }

		double sf(long long x, long long y) const { return double(y-x) / m_freq; }
		double sf(long long x) const { return double(x) / m_freq; }
#else
		long long ns(long long x) const { return x; }
		long long us(long long x) const	{ return x / 1000; }
		long long ms(long long x) const	{ return x / 1000000; }

		long long ns(long long x, long long y) const { return (y-x); }
		long long us(long long x, long long y) const { return (y-x) / 1000; }
		long long ms(long long x, long long y) const { return (y-x) / 1000000; }

		double nf(long long x) const { return x; }
		double uf(long long x) const { return x / 1e3; }
		double mf(long long x) const { return x / 1e6; }

		double nf(long long x, long long y) const { return (y-x); }
		double uf(long long x, long long y) const { return (y-x) / 1e3; }
		double mf(long long x, long long y) const { return (y-x) / 1e6; }

		double sf(long long x, long long y) const { return (y-x) / 1e9; }
		double sf(long long x) const { return x / 1e9; }
#endif
	};


#if defined(CLOCK_MONOTONIC) || defined(CLOCK_REALTIME)
// this is a very thin wrapper on clock_gettime, this wrapper
// can be optimized out by compiler
class qtime : public timespec { // Quick Time
public:
	// intentional: do not define construtor for qtime

	inline static qtime now() noexcept {
		qtime t;
		// for speed, intentional do not check clock_gettime return value,
		// since clock_gettime should always success with such CLOCK_XXX.
	#if defined(CLOCK_MONOTONIC)
		clock_gettime(CLOCK_MONOTONIC, &t);
	#elif defined(CLOCK_THREAD_CPUTIME_ID)
		clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
	#elif defined(CLOCK_PROCESS_CPUTIME_ID)
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
	#else
		clock_gettime(CLOCK_REALTIME, &t);
	#endif
		return t;
	}
	inline static qtime now(clockid_t clock) {
		qtime t;
		TERARK_VERIFY_F(clock_gettime(clock, &t) == 0,
					   "clock = %d : err = %m", clock);
		return t;
	}
	long long ns(const qtime& y) const noexcept { return (y.tv_sec - tv_sec) * 1000000000LL + (y.tv_nsec - tv_nsec); }
	long long us(const qtime& y) const noexcept { return (y.tv_sec - tv_sec) * 1000000LL + (y.tv_nsec - tv_nsec)/1000; }
	long long ms(const qtime& y) const noexcept { return (y.tv_sec - tv_sec) * 1000LL + (y.tv_nsec - tv_nsec)/1000000; }
	double nf(const qtime& y) const noexcept { return (y.tv_sec - tv_sec) * 1e9 + (y.tv_nsec - tv_nsec); }       // nano
	double uf(const qtime& y) const noexcept { return (y.tv_sec - tv_sec) * 1e6 + (y.tv_nsec - tv_nsec) / 1e3; } // micro
	double mf(const qtime& y) const noexcept { return (y.tv_sec - tv_sec) * 1e3 + (y.tv_nsec - tv_nsec) / 1e6; } // milli
	double sf(const qtime& y) const noexcept { return (y.tv_sec - tv_sec)       + (y.tv_nsec - tv_nsec) / 1e9; } // seconds
	double Mf(const qtime& y) const noexcept { return (y.tv_sec - tv_sec) /60.0 + (y.tv_nsec - tv_nsec) /60e9; } // Minutes
	double Hf(const qtime& y) const noexcept { return (y.tv_sec - tv_sec) /3600.0 + (y.tv_nsec - tv_nsec) /3600e9; } // Hours
	double Df(const qtime& y) const noexcept { return (y.tv_sec - tv_sec) /86400.0 + (y.tv_nsec - tv_nsec) /86400e9; } // Days
	class qduration {
		friend class qtime;
		long long dura;
		qduration(long long d) noexcept : dura(d) {}
	public:
		long long ns() const noexcept { return dura; }
		long long us() const noexcept { return dura/1000; }
		long long ms() const noexcept { return dura/1000000; }
		double nf() const noexcept { return dura; }     // nano
		double uf() const noexcept { return dura/1e3; } // micro
		double mf() const noexcept { return dura/1e6; } // milli
		double sf() const noexcept { return dura/1e9; } // seconds
		double Mf() const noexcept { return dura/60e9; } // Minutes
		double Hf() const noexcept { return dura/3600e9; } // Hours
		double Df() const noexcept { return dura/86400e9; } // Days
	};
	inline qduration operator-(const qtime& y) const noexcept {
		// this reduce an imul CPU instruction than using 'profiling'
		return (tv_sec - y.tv_sec) * 1000000000LL + (tv_nsec - y.tv_nsec);
	}
}; // class qtime
#else
class TERARK_DLL_EXPORT qtime { // Quick Time
	long long tp;
	static profiling& pf() noexcept;
public:
	explicit qtime(int /*clock ignored*/ = 0) noexcept { tp = pf().now(); }
	static qtime now() { return qtime(); }
	static qtime now(clockid_t clock) { return qtime(clock); }
	long long ns(const qtime& y) const noexcept { return pf().ns(tp, y.tp); }
	long long us(const qtime& y) const noexcept { return pf().us(tp, y.tp); }
	long long ms(const qtime& y) const noexcept { return pf().ms(tp, y.tp); }
	double nf(const qtime& y) const noexcept { return pf().nf(tp, y.tp); } // nano
	double uf(const qtime& y) const noexcept { return pf().uf(tp, y.tp); } // micro
	double mf(const qtime& y) const noexcept { return pf().mf(tp, y.tp); } // milli
	double sf(const qtime& y) const noexcept { return pf().sf(tp, y.tp); } // seconds
	double Mf(const qtime& y) const noexcept { return pf().sf(tp, y.tp)/60.0; } // Minutes
	double Hf(const qtime& y) const noexcept { return pf().sf(tp, y.tp)/3600.0; } // Hours
	double Df(const qtime& y) const noexcept { return pf().sf(tp, y.tp)/86400.0; } // Days
	class qduration {
		friend class qtime;
		long long dura;
		qduration(long long d) noexcept : dura(d) {}
	public:
		long long ns() const noexcept { return qtime::pf().ns(dura); }
		long long us() const noexcept { return qtime::pf().us(dura); }
		long long ms() const noexcept { return qtime::pf().ms(dura); }
		double nf() const noexcept { return qtime::pf().nf(dura); }
		double uf() const noexcept { return qtime::pf().uf(dura); }
		double mf() const noexcept { return qtime::pf().mf(dura); }
		double sf() const noexcept { return qtime::pf().sf(dura); }
		double Mf() const noexcept { return qtime::pf().sf(dura)/60.0; }
		double Hf() const noexcept { return qtime::pf().sf(dura)/3600.0; }
		double Df() const noexcept { return qtime::pf().sf(dura)/86400.0; }
	};
	friend class qduration; // for pf()
	qduration operator-(const qtime& y) const noexcept { return tp - y.tp; }
};
#endif

// defined in nolocks_localtime.cpp
TERARK_DLL_EXPORT const char* StrDateTimeNow();

}
