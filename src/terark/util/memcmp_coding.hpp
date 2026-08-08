#pragma once

#include <terark/config.hpp>
#include <terark/io/byte_swap.hpp>
#include <terark/stdtypes.hpp>

#include <stdint.h>
#include <type_traits>

namespace terark {

TERARK_DLL_EXPORT
char* encode_0_01_00(const char* ibeg, const char* iend, char* obeg, char* oend);

TERARK_DLL_EXPORT
char* decode_01_00(const char* ibeg, const char**ires, char* obeg, char* oend);

TERARK_DLL_EXPORT
const char* end_of_01_00(const char* encoded);

TERARK_DLL_EXPORT
const char* end_of_01_00(const char* beg, const char* end);

namespace memcmp_coding_detail {

template<class Real>
struct real_uint {
  static_assert(sizeof(Real) == sizeof(uint32_t) ||
                sizeof(Real) == sizeof(uint64_t),
                "memcmp real encoding requires a 32- or 64-bit type");
  typedef typename std::conditional<
      sizeof(Real) == sizeof(uint32_t), uint32_t, uint64_t>::type type;
};

template<class UInt>
inline UInt byte_swap_if_little_endian(UInt value) {
#if defined(BOOST_ENDIAN_LITTLE_BYTE)
  return byte_swap(value);
#elif defined(BOOST_ENDIAN_BIG_BYTE)
  return value;
#else
# error "unknown byte order"
#endif
}

} // namespace memcmp_coding_detail

template<class Real>
inline unsigned char* encode_memcmp_real(Real nr, unsigned char* dst) {
  typedef typename memcmp_coding_detail::real_uint<Real>::type Uint;
  static const int Bits = sizeof(Real)*8;
  static const Uint SignBit = Uint(1) << (Bits - 1);
  Uint ui = aligned_load<Uint>(&nr);

  // FoundationDB tuple-layer float encoding: negative values have every bit
  // inverted; non-negative values have only the sign bit inverted. Writing the
  // result in big-endian order makes bytewise comparison match IEEE 754
  // totalOrder, including signed zero, infinities, and NaNs.
  ui = ui & SignBit ? ~ui : ui ^ SignBit;
  unaligned_save(dst,
                 memcmp_coding_detail::byte_swap_if_little_endian(ui));
  return dst + sizeof(Real);
}

template<class Real>
inline const unsigned char*
decode_memcmp_real(const unsigned char* src, Real* dst) {
  typedef typename memcmp_coding_detail::real_uint<Real>::type Uint;
  static const int Bits = sizeof(Real)*8;
  static const Uint SignBit = Uint(1) << (Bits - 1);
  Uint ui = memcmp_coding_detail::byte_swap_if_little_endian(
      unaligned_load<Uint>(src));

  // The encoded sign bit is set for original non-negative values.
  ui = ui & SignBit ? ui ^ SignBit : ~ui;
  aligned_save(dst, ui);
  return src + sizeof(Real);
}

// float encoding/decoding intentionally use unsigned char*
inline unsigned char* encode_memcmp_float(float src, unsigned char* dst) {
  return encode_memcmp_real<float>(src, dst);
}

inline unsigned char* encode_memcmp_double(double src, unsigned char* dst) {
  return encode_memcmp_real<double>(src, dst);
}

inline const unsigned char*
decode_memcmp_float(const unsigned char* src, float* dst) {
  return decode_memcmp_real<float>(src, dst);
}

inline const unsigned char*
decode_memcmp_double(const unsigned char* src, double* dst) {
  return decode_memcmp_real<double>(src, dst);
}

} // namespace terark
