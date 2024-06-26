#include <terark/io/DataIO_Basic.hpp>
#include <terark/node_layout.hpp> // for bytes2uint
#include "memcmp_coding.hpp"
#include <float.h>

namespace terark {

TERARK_DLL_EXPORT
char* encode_0_01_00(const char* ibeg, const char* iend, char* obeg, char* oend) {
  for (; ibeg < iend; ++ibeg) {
    TERARK_VERIFY_F(obeg < oend, "broken data: input remain bytes = %zd",
                    iend - ibeg);
    char b = *ibeg;
    if (terark_likely(0 != b)) {
      *obeg++ = b;
    }
    else {
      TERARK_VERIFY_F(obeg + 1 < oend, "broken data: input remain bytes = %zd",
                      iend - ibeg);
      obeg[0] = 0;
      obeg[1] = 1; // 0 -> 01
      obeg += 2;
    }
  }
  TERARK_VERIFY_F(obeg + 1 < oend, "broken data: input remain bytes = %zd",
                  iend - ibeg);
  obeg[0] = obeg[1] = 0; // end with 00
  return obeg + 2;
}

///@param ires (*ires)+1 point to next byte after ending 00,
///             this is different to return value
///@returns returns output end pos
TERARK_DLL_EXPORT
char* decode_01_00(const char* ibeg, const char** ires, char* obeg, char* oend) {
  const char* icur = ibeg;
  while (true) {
    TERARK_VERIFY_F(obeg < oend, "broken data: decoded input bytes = %zd",
                    icur - ibeg);
    char b = *icur;
    if (terark_likely(0 != b)) {
      *obeg++ = b;
      icur++;
    }
    else {
      b = icur[1];
      if (1 == b) { // 01 -> 0
        *obeg++ = 0;
        icur += 2;
      }
      else {
        // if b is 0, it is ok
        // if b is not 0, byte(b) >= 2, and is error
        break;
      }
    }
  }
  // if (*ires)[-1] is 0, it is ok, ires+1 is the next decoding byte
  // if (*ires)[-1] is n, it is error, where n >= 2
  *ires = icur + 2;
  return obeg;
}

///@returns next byte pos after ending 0n
TERARK_DLL_EXPORT
const char* end_of_01_00(const char* encoded) {
  while (true) {
    if (encoded[0])
      encoded++;
    if (1 == encoded[1])
      encoded += 2;
    else
      return encoded + 2; // OK if ret[-1] is 0, else error
  }
}

TERARK_DLL_EXPORT
const char* end_of_01_00(const char* encoded, const char* end) {
  while (encoded < end) {
    if (encoded[0])
      encoded++;
    else if (encoded + 1 < end) {
      if (1 == encoded[1])
        encoded += 2;
      else
        return encoded + 2; // OK if ret[-1] is 0, else error
    }
    else
      return end + 1; // error, 0n where n is out of bound
  }
  return end;
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
    unaligned_save(dst, BIG_ENDIAN_OF(ui));
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
