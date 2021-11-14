#include <terark/io/DataIO_Basic.hpp>
#include <terark/node_layout.hpp> // for bytes2uint
#include "memcmp_coding.hpp"
#include <float.h>

namespace terark {

TERARK_DLL_EXPORT
char* encode_00_0n(const char* ibeg, const char* iend, char* obeg, char* oend, char out_end_mark) {
  TERARK_VERIFY(0 != out_end_mark);
  for (; ibeg < iend; ++ibeg) {
    TERARK_VERIFY_F(obeg < oend, "broken data: input remain bytes = %zd",
                     iend - ibeg);
    char b = *ibeg;
    if (terark_likely(0 != b)) {
      *obeg++ = b;
    }
    else {
      obeg[0] = obeg[1] = 0; // 0 -> 00
      obeg += 2;
    }
  }
  obeg[0] = 0;
  obeg[1] = out_end_mark;
  return obeg + 2;
}

///@param ires *ires point to next byte after ending 0n,
///             this is different to return value
///@returns returns output end pos, do NOT decode ending 0n!
///@note ending 0n will NOT be written to output
TERARK_DLL_EXPORT
char* decode_00_0n(const char* ibeg, const char** ires, char* obeg, char* oend) {
  const char* icur = ibeg;
  for (; ; ++obeg) {
    TERARK_VERIFY_F(obeg < oend, "broken data: decoded input bytes = %zd",
                     icur - ibeg);
    char b = *icur;
    if (terark_likely(0 != b)) {
      *obeg = b;
      icur++;
    }
    else {
      b = icur[1];
      if (0 != b) {
        // do not decode ending 0n
        // obeg[0] = 0;
        // obeg[1] = b; // out_end_mark in encode_00_0n
        break;
      }
      else { // 00 -> 0
        *obeg = 0;
        icur += 2;
      }
    }
  }
  *ires = icur + 2;
  return obeg;
}

///@returns next byte pos after ending 0n
TERARK_DLL_EXPORT
const char* end_of_00_0n(const char* encoded) {
  while (true) {
    if (encoded[0])
      encoded++;
    else if (encoded[1]) // 0n
      return encoded + 2;
    else // 00
      encoded += 2;
  }
}

static const int FLT_EXP_DIG = (sizeof(float )*8-FLT_MANT_DIG);
static const int DBL_EXP_DIG = (sizeof(double)*8-DBL_MANT_DIG);

template<class Real>
TERARK_DLL_EXPORT
unsigned char* encode_memcmp_real(Real nr, unsigned char* dst) {
  const int ExpDigit = sizeof(Real) == 4 ? FLT_EXP_DIG : DBL_EXP_DIG;
  if (nr == 0.0) { /* Change to zero string */
    memset(dst, 0, sizeof(Real));
    dst[0] = (unsigned char)128;
  }
  else {
    typedef typename bytes2uint<sizeof(Real)>::type Uint;
    static const int Bits = sizeof(Real)*8;
    Uint ui = aligned_load<Uint>(&nr);
    if (ui & Uint(1) << (Bits - 1)) {
      ui = ~ui;
    } else { /* Set high and move exponent one up */
      ui |= Uint(1) << (Bits - 1);
      ui += Uint(1) << (Bits - 1 - ExpDigit);
    }
    unaligned_save(dst, VALUE_OF_BYTE_SWAP_IF_LITTLE_ENDIAN(ui));
  }
  return dst + sizeof(Real);
}

template<class Real>
TERARK_DLL_EXPORT
const unsigned char*
decode_memcmp_real(const unsigned char* src, Real* dst) {
  const int ExpDigit = sizeof(Real) == 4 ? FLT_EXP_DIG : DBL_EXP_DIG;
  const static Real zero_val = 0.0;
  const static unsigned char zero_pattern[sizeof(Real)] = {128, 0};

  /* Check to see if the value is zero */
  if (memcmp(src, zero_pattern, sizeof(Real)) == 0) {
    *dst = zero_val;
  }
  else {
    typedef typename bytes2uint<sizeof(Real)>::type Uint;
    static const int Bits = sizeof(Real)*8;
    Uint ui = unaligned_load<Uint>(src);
    BYTE_SWAP_IF_LITTLE_ENDIAN(ui);
    if (ui & Uint(1) << (Bits - 1)) {
      // If the high bit is set the original value was positive so
      // remove the high bit and subtract one from the exponent.
      ui -=  Uint(1) << (Bits - 1 - ExpDigit); // subtract from exponent
      ui &= ~Uint(0) >> 1;
    } else {
      // Otherwise the original value was negative and all bytes have been
      // negated.
      ui = ~ui;
    }
    aligned_save(dst, ui);
  }
  return src + sizeof(Real);
}

TERARK_DLL_EXPORT template
unsigned char* encode_memcmp_real<float>(float nr, unsigned char* dst);
TERARK_DLL_EXPORT template
unsigned char* encode_memcmp_real<double>(double nr, unsigned char* dst);

TERARK_DLL_EXPORT unsigned char*
encode_memcmp_float(float src, unsigned char* dst) {
  return encode_memcmp_real<float>(src, dst);
}
TERARK_DLL_EXPORT unsigned char*
encode_memcmp_double(double src, unsigned char* dst) {
  return encode_memcmp_real<double>(src, dst);
}

TERARK_DLL_EXPORT template
const unsigned char*
decode_memcmp_real<float>(const unsigned char* src, float* dst);
TERARK_DLL_EXPORT template
const unsigned char*
decode_memcmp_real<double>(const unsigned char* src, double* dst);

TERARK_DLL_EXPORT const unsigned char*
decode_memcmp_float(const unsigned char* src, float* dst) {
  return decode_memcmp_real<float>(src, dst);
}
TERARK_DLL_EXPORT const unsigned char*
decode_memcmp_double(const unsigned char* src, double* dst) {
  return decode_memcmp_real<double>(src, dst);
}

} // namespace
