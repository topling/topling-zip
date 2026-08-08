#include <terark/io/DataIO_Basic.hpp>
#include "memcmp_coding.hpp"

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

} // namespace
