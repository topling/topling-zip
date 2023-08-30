#pragma once

#include <terark/config.hpp>

namespace terark {

TERARK_DLL_EXPORT
char* encode_0_01_00(const char* ibeg, const char* iend, char* obeg, char* oend);

TERARK_DLL_EXPORT
char* decode_01_00(const char* ibeg, const char**ires, char* obeg, char* oend);

TERARK_DLL_EXPORT
const char* end_of_01_00(const char* encoded);

TERARK_DLL_EXPORT
const char* end_of_01_00(const char* beg, const char* end);

// float encoding/decoding intentinally use unsigned char*
TERARK_DLL_EXPORT
unsigned char* encode_memcmp_float(float src, unsigned char* dst);

TERARK_DLL_EXPORT
unsigned char* encode_memcmp_double(double src, unsigned char* dst);

TERARK_DLL_EXPORT const unsigned char*
decode_memcmp_float(const unsigned char* src, float* dst);

TERARK_DLL_EXPORT const unsigned char*
decode_memcmp_double(const unsigned char* src, double* dst);

template<class Real>
TERARK_DLL_EXPORT
unsigned char* encode_memcmp_real(Real src, unsigned char* dst);

template<class Real>
TERARK_DLL_EXPORT
const unsigned char*
decode_memcmp_real(const unsigned char* src, Real* dst);

} // namespace terark
