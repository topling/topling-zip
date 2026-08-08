#pragma once

#include <terark/config.hpp>
#include <terark/fstring.hpp>
#include <terark/io/DataIO_Exception.hpp>
#include <terark/io/byte_swap.hpp>
#include <terark/pass_by_value.hpp>
#include <terark/stdtypes.hpp>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include <type_traits>

namespace terark {

TERARK_DLL_EXPORT
char* encode_0_01_00(const char* ibeg, const char* iend, char* obeg, char* oend);

TERARK_DLL_EXPORT
void encode_0_01_00_append(fstring input, std::string* output);

TERARK_DLL_EXPORT
char* decode_01_00(const char* ibeg, const char**ires, char* obeg, char* oend);

TERARK_DLL_EXPORT
size_t decode_01_00(fstring encoded, std::string* decoded);

TERARK_DLL_EXPORT
const char* end_of_01_00(const char* encoded);

TERARK_DLL_EXPORT
const char* end_of_01_00(const char* beg, const char* end);

template<class UInt>
inline UInt memcmp_byte_swap_if_little_endian(UInt value) {
#if BOOST_ENDIAN_LITTLE_BYTE
  typedef typename std::make_unsigned<UInt>::type ByteSwapUInt;
  return static_cast<UInt>(byte_swap(static_cast<ByteSwapUInt>(value)));
#elif BOOST_ENDIAN_BIG_BYTE
  return value;
#else
# error "unknown byte order"
#endif
}

template<class Real>
inline unsigned char* encode_memcmp_real(Real nr, unsigned char* dst) {
  static_assert(sizeof(Real) == sizeof(uint32_t) ||
                sizeof(Real) == sizeof(uint64_t),
                "memcmp real encoding requires a 32- or 64-bit type");
  typedef typename std::conditional<
      sizeof(Real) == sizeof(uint32_t), uint32_t, uint64_t>::type Uint;
  static const int Bits = sizeof(Real)*8;
  static const Uint SignBit = Uint(1) << (Bits - 1);
  Uint ui = aligned_load<Uint>(&nr);

  // FoundationDB tuple-layer float encoding: negative values have every bit
  // inverted; non-negative values have only the sign bit inverted. Writing the
  // result in big-endian order makes bytewise comparison match IEEE 754
  // totalOrder, including signed zero, infinities, and NaNs.
  ui = ui & SignBit ? ~ui : ui ^ SignBit;
  unaligned_save(dst, memcmp_byte_swap_if_little_endian(ui));
  return dst + sizeof(Real);
}

template<class Real>
inline const unsigned char*
decode_memcmp_real(const unsigned char* src, Real* dst) {
  static_assert(sizeof(Real) == sizeof(uint32_t) ||
                sizeof(Real) == sizeof(uint64_t),
                "memcmp real decoding requires a 32- or 64-bit type");
  typedef typename std::conditional<
      sizeof(Real) == sizeof(uint32_t), uint32_t, uint64_t>::type Uint;
  static const int Bits = sizeof(Real)*8;
  static const Uint SignBit = Uint(1) << (Bits - 1);
  Uint ui = memcmp_byte_swap_if_little_endian(unaligned_load<Uint>(src));

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

///////////////////////////////////////////////////////////////////////////////
// DataIO adapters
//
// Use like as_var_int:
//   DATA_IO_LOAD_SAVE(MyData, &as_memcmp(key))
//   dio & as_memcmp(value); // load or save
//
// Encoded bytes compare in value order. The proxy borrows value and should be
// used in the DataIO expression.
///////////////////////////////////////////////////////////////////////////////

#if BOOST_ENDIAN_LITTLE_BYTE

template<class T>
class as_big_endian_ref {
public:
  T& value;
  explicit as_big_endian_ref(T& value) : value(value) {
    typedef typename std::remove_const<T>::type UInt;
    static_assert(std::is_integral<UInt>::value &&
                  std::is_unsigned<UInt>::value &&
                  !std::is_same<UInt, bool>::value,
                  "as_big_endian requires an unsigned integer");
  }

  template<class Output>
  friend void DataIO_saveObject(Output& out, as_big_endian_ref x) {
    typedef typename std::remove_const<T>::type UInt;
    UInt encoded = memcmp_byte_swap_if_little_endian(x.value);
    out.ensureWrite(&encoded, sizeof(encoded));
  }

  template<class Input>
  friend void DataIO_loadObject(Input& in, as_big_endian_ref x) {
    static_assert(!std::is_const<T>::value,
                  "cannot load into a const unsigned integer");
    T encoded;
    in.ensureRead(&encoded, sizeof(encoded));
    x.value = memcmp_byte_swap_if_little_endian(encoded);
  }
};

template<class T>
inline pass_by_value<as_big_endian_ref<T> > as_big_endian(T& value) {
  return pass_by_value<as_big_endian_ref<T> >(as_big_endian_ref<T>(value));
}

template<class T>
inline pass_by_value<as_big_endian_ref<const T> >
as_big_endian(const T&& value) {
  return pass_by_value<as_big_endian_ref<const T> >(
      as_big_endian_ref<const T>(value));
}

#elif BOOST_ENDIAN_BIG_BYTE

template<class T>
inline T& as_big_endian(T& value) {
  typedef typename std::remove_const<T>::type UInt;
  static_assert(std::is_integral<UInt>::value &&
                std::is_unsigned<UInt>::value &&
                !std::is_same<UInt, bool>::value,
                "as_big_endian requires an unsigned integer");
  return value;
}

template<class T>
inline const T& as_big_endian(const T&& value) {
  static_assert(std::is_integral<T>::value && std::is_unsigned<T>::value &&
                !std::is_same<T, bool>::value,
                "as_big_endian requires an unsigned integer");
  return value;
}

#else
# error "unknown byte order"
#endif

template<class T>
class as_memcmp_signed_ref {
public:
  T& value;
  explicit as_memcmp_signed_ref(T& value) : value(value) {
    typedef typename std::remove_const<T>::type Int;
    static_assert(std::is_integral<Int>::value &&
                  std::is_signed<Int>::value,
                  "as_memcmp_signed_ref requires a signed integer");
  }

  template<class Output>
  friend void DataIO_saveObject(Output& out, as_memcmp_signed_ref x) {
    typedef typename std::remove_const<T>::type Int;
    typedef typename std::make_unsigned<Int>::type UInt;
    UInt bits;
    memcpy(&bits, &x.value, sizeof(bits));
    bits ^= UInt(1) << (sizeof(bits) * 8 - 1);
    bits = memcmp_byte_swap_if_little_endian(bits);
    out.ensureWrite(&bits, sizeof(bits));
  }

  template<class Input>
  friend void DataIO_loadObject(Input& in, as_memcmp_signed_ref x) {
    static_assert(!std::is_const<T>::value,
                  "cannot load into a const signed integer");
    typedef typename std::make_unsigned<T>::type UInt;
    UInt bits;
    in.ensureRead(&bits, sizeof(bits));
    bits = memcmp_byte_swap_if_little_endian(bits);
    bits ^= UInt(1) << (sizeof(bits) * 8 - 1);
    memcpy(&x.value, &bits, sizeof(bits));
  }
};

template<class T>
class as_memcmp_bool_ref {
public:
  T& value;
  explicit as_memcmp_bool_ref(T& value) : value(value) {
    static_assert(
        std::is_same<typename std::remove_const<T>::type, bool>::value,
        "as_memcmp_bool_ref requires bool");
  }

  template<class Output>
  friend void DataIO_saveObject(Output& out, as_memcmp_bool_ref x) {
    out.writeByte(static_cast<unsigned char>(x.value));
  }

  template<class Input>
  friend void DataIO_loadObject(Input& in, as_memcmp_bool_ref x) {
    static_assert(!std::is_const<T>::value, "cannot load into a const bool");
    const unsigned char encoded = in.readByte();
    if (encoded > 1) {
      throw DataFormatException("invalid as_memcmp bool encoding");
    }
    x.value = encoded != 0;
  }
};

template<class T>
class as_memcmp_real_ref {
public:
  T& value;
  explicit as_memcmp_real_ref(T& value) : value(value) {
    typedef typename std::remove_const<T>::type Real;
    static_assert(std::is_same<Real, float>::value ||
                  std::is_same<Real, double>::value,
                  "as_memcmp_real_ref requires float or double");
  }

  template<class Output>
  friend void DataIO_saveObject(Output& out, as_memcmp_real_ref x) {
    unsigned char encoded[sizeof(T)];
    encode_memcmp_real(x.value, encoded);
    out.ensureWrite(encoded, sizeof(encoded));
  }

  template<class Input>
  friend void DataIO_loadObject(Input& in, as_memcmp_real_ref x) {
    static_assert(!std::is_const<T>::value, "cannot load into a const real");
    unsigned char encoded[sizeof(T)];
    in.ensureRead(encoded, sizeof(encoded));
    decode_memcmp_real(encoded, &x.value);
  }
};

template<class T>
class as_memcmp_string_ref {
public:
  T& value;
  explicit as_memcmp_string_ref(T& value) : value(value) {
    static_assert(
        std::is_same<typename std::remove_const<T>::type, std::string>::value,
        "as_memcmp_string_ref requires std::string");
  }

  template<class Output>
  friend void DataIO_saveObject(Output& out, as_memcmp_string_ref x) {
    const unsigned char escaped_zero[] = {0, 1};
    const unsigned char terminator[] = {0, 0};
    const char* current = x.value.data();
    const char* const end = current + x.value.size();
    while (current != end) {
      auto zero = static_cast<const char*>(memchr(current, 0, end - current));
      if (zero == NULL) {
        out.ensureWrite(current, end - current);
        break;
      }
      if (zero != current) {
        out.ensureWrite(current, zero - current);
      }
      out.ensureWrite(escaped_zero, sizeof(escaped_zero));
      current = zero + 1;
    }
    out.ensureWrite(terminator, sizeof(terminator));
  }

  template<class Input>
  friend void DataIO_loadObject(Input& in, as_memcmp_string_ref x) {
    static_assert(!std::is_const<T>::value, "cannot load into a const string");
    x.value.clear();
    size_t used = 0;
    size_t size = 15;
    try {
      string_resize_no_touch_memory(&x.value, size);
      for (;;) {
        unsigned char ch = in.readByte();
        if (ch == 0) {
          const unsigned char escape = in.readByte();
          if (escape == 0) {
            break;
          }
          if (escape != 1) {
            throw DataFormatException("invalid as_memcmp string escape");
          }
        }
        if (used == size) {
          size = size * 103 / 64;
          string_resize_no_touch_memory(&x.value, size);
        }
        x.value[used++] = static_cast<char>(ch);
      }
    }
    catch (...) {
      x.value.resize(used);
      throw;
    }
    x.value.resize(used);
  }
};

/**
 * Wrap a value for DataIO serialization whose byte order matches value order.
 *
 * Integers use fixed-width big-endian encoding, with the sign bit flipped for
 * signed types. float and double use the FoundationDB-compatible transform
 * above. Strings escape NUL as 00 01 and terminate with 00 00, so prefixes and
 * embedded NUL bytes remain ordered and independently decodable.
 *
 * The returned proxy borrows value and must not outlive it. Keep the proxy in
 * the same full expression as the DataIO operation.
 */
template<class T>
inline typename std::enable_if<
    std::is_integral<T>::value && std::is_signed<T>::value,
    pass_by_value<as_memcmp_signed_ref<T> >
  >::type
as_memcmp(T& value) {
  return pass_by_value<as_memcmp_signed_ref<T> >(
      as_memcmp_signed_ref<T>(value));
}

template<class T>
inline typename std::enable_if<
    std::is_integral<T>::value && std::is_signed<T>::value,
    pass_by_value<as_memcmp_signed_ref<const T> >
  >::type
as_memcmp(const T& value) {
  return pass_by_value<as_memcmp_signed_ref<const T> >(
      as_memcmp_signed_ref<const T>(value));
}

template<class T>
inline auto as_memcmp(T& value) -> typename std::enable_if<
    std::is_integral<T>::value && std::is_unsigned<T>::value,
    decltype(as_big_endian(value))
  >::type {
  return as_big_endian(value);
}

template<class T>
inline auto as_memcmp(const T& value) -> typename std::enable_if<
    std::is_integral<T>::value && std::is_unsigned<T>::value,
    decltype(as_big_endian(value))
  >::type {
  return as_big_endian(value);
}

inline pass_by_value<as_memcmp_bool_ref<bool> > as_memcmp(bool& value) {
  return pass_by_value<as_memcmp_bool_ref<bool> >(
      as_memcmp_bool_ref<bool>(value));
}

inline pass_by_value<as_memcmp_bool_ref<const bool> >
as_memcmp(const bool& value) {
  return pass_by_value<as_memcmp_bool_ref<const bool> >(
      as_memcmp_bool_ref<const bool>(value));
}

inline pass_by_value<as_memcmp_real_ref<float> > as_memcmp(float& value) {
  return pass_by_value<as_memcmp_real_ref<float> >(
      as_memcmp_real_ref<float>(value));
}

inline pass_by_value<as_memcmp_real_ref<const float> >
as_memcmp(const float& value) {
  return pass_by_value<as_memcmp_real_ref<const float> >(
      as_memcmp_real_ref<const float>(value));
}

inline pass_by_value<as_memcmp_real_ref<double> > as_memcmp(double& value) {
  return pass_by_value<as_memcmp_real_ref<double> >(
      as_memcmp_real_ref<double>(value));
}

inline pass_by_value<as_memcmp_real_ref<const double> >
as_memcmp(const double& value) {
  return pass_by_value<as_memcmp_real_ref<const double> >(
      as_memcmp_real_ref<const double>(value));
}

inline pass_by_value<as_memcmp_string_ref<std::string> >
as_memcmp(std::string& value) {
  return pass_by_value<as_memcmp_string_ref<std::string> >(
      as_memcmp_string_ref<std::string>(value));
}

inline pass_by_value<as_memcmp_string_ref<const std::string> >
as_memcmp(const std::string& value) {
  return pass_by_value<as_memcmp_string_ref<const std::string> >(
      as_memcmp_string_ref<const std::string>(value));
}

} // namespace terark
