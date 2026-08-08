#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits>
#include <string>
#include <utility>
#include <vector>
#include <terark/io/DataInput.hpp>
#include <terark/io/DataOutput.hpp>
#include <terark/io/MemStream.hpp>
#include <terark/util/memcmp_coding.hpp>
#include <terark/util/function.hpp>
#include <terark/stdtypes.hpp>
#include <terark/fstring.hpp>

using namespace terark;

static_assert(std::is_same<
                  decltype(as_memcmp(std::declval<int32_t&>())),
                  pass_by_value<as_memcmp_signed_ref<int32_t> >
              >::value,
              "signed integers must use the signed memcmp proxy");
static_assert(std::is_same<
                  decltype(as_memcmp(std::declval<uint32_t&>())),
                  decltype(as_big_endian(std::declval<uint32_t&>()))
              >::value,
              "unsigned integers must use as_big_endian");
#if BOOST_ENDIAN_BIG_BYTE
static_assert(std::is_same<
                  decltype(as_big_endian(std::declval<uint32_t&>())),
                  uint32_t&
              >::value,
              "as_big_endian must return its argument on big-endian hosts");
#else
static_assert(std::is_same<
                  decltype(as_big_endian(std::declval<uint32_t&>())),
                  pass_by_value<as_big_endian_ref<uint32_t> >
              >::value,
              "as_big_endian must use a byte-swapping proxy on little-endian hosts");
#endif
static_assert(std::is_same<
                  decltype(as_memcmp(std::declval<float&>())),
                  pass_by_value<as_memcmp_real_ref<float> >
              >::value,
              "real numbers must use the real memcmp proxy");
static_assert(std::is_same<
                  decltype(as_memcmp(std::declval<std::string&>())),
                  pass_by_value<as_memcmp_string_ref<std::string> >
              >::value,
              "strings must use the string memcmp proxy");
static_assert(std::is_same<
                  decltype(as_memcmp(std::declval<bool&>())),
                  pass_by_value<as_memcmp_bool_ref<bool> >
              >::value,
              "bool must use the bool memcmp proxy");

class MemcmpTestOutput {
public:
    std::vector<unsigned char> data;
    size_t ensure_write_calls;
    size_t write_byte_calls;

    MemcmpTestOutput() : ensure_write_calls(0), write_byte_calls(0) {}

    void ensureWrite(const void* source, size_t size) {
        ++ensure_write_calls;
        const unsigned char* bytes =
            static_cast<const unsigned char*>(source);
        data.insert(data.end(), bytes, bytes + size);
    }

    void writeByte(unsigned char byte) {
        ++write_byte_calls;
        data.push_back(byte);
    }

    template<class T>
    MemcmpTestOutput& operator<<(pass_by_value<T> value) {
        DataIO_saveObject(*this, value.val);
        return *this;
    }

    template<class T>
    typename std::enable_if<
        std::is_integral<T>::value && std::is_unsigned<T>::value &&
        !std::is_same<T, bool>::value,
        MemcmpTestOutput&
    >::type
    operator<<(const T& value) {
        ensureWrite(&value, sizeof(value));
        return *this;
    }
};

class MemcmpTestInput {
    const std::vector<unsigned char>& data;
    size_t position;

public:
    explicit MemcmpTestInput(const std::vector<unsigned char>& data)
      : data(data), position(0) {}

    void ensureRead(void* destination, size_t size) {
        TERARK_VERIFY_F(size <= data.size() - position,
                        "read past end: pos=%zd size=%zd available=%zd",
                        position, size, data.size());
        memcpy(destination, data.data() + position, size);
        position += size;
    }

    unsigned char readByte() {
        TERARK_VERIFY_F(position < data.size(),
                        "read past end: pos=%zd available=%zd",
                        position, data.size());
        return data[position++];
    }

    size_t tell() const {
        return position;
    }

    template<class T>
    MemcmpTestInput& operator>>(pass_by_value<T> value) {
        DataIO_loadObject(*this, value.val);
        return *this;
    }

    template<class T>
    typename std::enable_if<
        std::is_integral<T>::value && std::is_unsigned<T>::value &&
        !std::is_same<T, bool>::value,
        MemcmpTestInput&
    >::type
    operator>>(T& value) {
        ensureRead(&value, sizeof(value));
        return *this;
    }
};

class TruncatedMemcmpTestInput {
    const std::vector<unsigned char>& data;
    size_t position;

public:
    explicit TruncatedMemcmpTestInput(
        const std::vector<unsigned char>& data)
      : data(data), position(0) {}

    unsigned char readByte() {
        if (position == data.size()) {
            throw DataFormatException("truncated as_memcmp string");
        }
        return data[position++];
    }

    template<class T>
    TruncatedMemcmpTestInput& operator>>(pass_by_value<T> value) {
        DataIO_loadObject(*this, value.val);
        return *this;
    }
};

template<class T>
std::vector<unsigned char> encode_with_as_memcmp(const T& value) {
    MemcmpTestOutput output;
    output << as_memcmp(value);
    return output.data;
}

template<class T, size_t Size>
void verify_as_memcmp_numeric_order(const T (&values)[Size]) {
    std::vector<unsigned char> previous;
    for (size_t i = 0; i < Size; ++i) {
        const std::vector<unsigned char> encoded =
            encode_with_as_memcmp(values[i]);
        TERARK_VERIFY_EQ(encoded.size(), sizeof(T));
        if (i != 0) {
            TERARK_VERIFY_F(memcmp(previous.data(), encoded.data(),
                                   sizeof(T)) < 0,
                            "as_memcmp numeric order mismatch at %zd", i);
        }

        T decoded = T();
        MemcmpTestInput input(encoded);
        input >> as_memcmp(decoded);
        TERARK_VERIFY_EQ(input.tell(), encoded.size());
        TERARK_VERIFY_F(memcmp(&decoded, &values[i], sizeof(T)) == 0,
                        "as_memcmp numeric round trip mismatch at %zd", i);
        previous = encoded;
    }
}

template<class T, size_t Size>
void verify_as_memcmp_bytes(
    const T& value,
    const unsigned char (&expected)[Size]) {
    const std::vector<unsigned char> encoded = encode_with_as_memcmp(value);
    TERARK_VERIFY_EQ(encoded.size(), Size);
    TERARK_VERIFY_F(memcmp(encoded.data(), expected, Size) == 0,
                    "as_memcmp encoded bytes mismatch");
}

void test_as_memcmp_numbers() {
    const int8_t signed8[] = {
        std::numeric_limits<int8_t>::min(), -1, 0, 1,
        std::numeric_limits<int8_t>::max(),
    };
    const uint8_t unsigned8[] = {
        0, 1, 2, 127, 128, std::numeric_limits<uint8_t>::max(),
    };
    const int16_t signed16[] = {
        std::numeric_limits<int16_t>::min(), -123, -1, 0, 1, 123,
        std::numeric_limits<int16_t>::max(),
    };
    const uint16_t unsigned16[] = {
        0, 1, 255, 256, 32768, std::numeric_limits<uint16_t>::max(),
    };
    const char16_t chars16[] = {
        0, 1, 0x1234, std::numeric_limits<char16_t>::max(),
    };
    const int32_t signed32[] = {
        std::numeric_limits<int32_t>::min(), -1234567, -1, 0, 1, 1234567,
        std::numeric_limits<int32_t>::max(),
    };
    const uint32_t unsigned32[] = {
        0, 1, 65535, 65536, UINT32_C(0x80000000), UINT32_MAX,
    };
    const int64_t signed64[] = {
        std::numeric_limits<int64_t>::min(), INT64_C(-1234567890123), -1, 0,
        1, INT64_C(1234567890123), std::numeric_limits<int64_t>::max(),
    };
    const uint64_t unsigned64[] = {
        0, 1, UINT64_C(0xFFFFFFFF), UINT64_C(0x100000000),
        UINT64_C(0x8000000000000000), UINT64_MAX,
    };
    const float floats[] = {
        -std::numeric_limits<float>::infinity(), -123.5F, -0.0F, +0.0F,
        +123.5F, +std::numeric_limits<float>::infinity(),
    };
    const double doubles[] = {
        -std::numeric_limits<double>::infinity(), -123.5, -0.0, +0.0,
        +123.5, +std::numeric_limits<double>::infinity(),
    };
    const bool booleans[] = {false, true};

    verify_as_memcmp_numeric_order(signed8);
    verify_as_memcmp_numeric_order(unsigned8);
    verify_as_memcmp_numeric_order(signed16);
    verify_as_memcmp_numeric_order(unsigned16);
    verify_as_memcmp_numeric_order(chars16);
    verify_as_memcmp_numeric_order(signed32);
    verify_as_memcmp_numeric_order(unsigned32);
    verify_as_memcmp_numeric_order(signed64);
    verify_as_memcmp_numeric_order(unsigned64);
    verify_as_memcmp_numeric_order(floats);
    verify_as_memcmp_numeric_order(doubles);
    verify_as_memcmp_numeric_order(booleans);

    const unsigned char int32_min[] = {0x00, 0x00, 0x00, 0x00};
    const unsigned char int32_minus_one[] = {0x7F, 0xFF, 0xFF, 0xFF};
    const unsigned char int32_zero[] = {0x80, 0x00, 0x00, 0x00};
    const unsigned char int32_max[] = {0xFF, 0xFF, 0xFF, 0xFF};
    const unsigned char uint32_value[] = {0x12, 0x34, 0x56, 0x78};
    const unsigned char char16_value[] = {0x12, 0x34};
    verify_as_memcmp_bytes(std::numeric_limits<int32_t>::min(), int32_min);
    verify_as_memcmp_bytes(int32_t(-1), int32_minus_one);
    verify_as_memcmp_bytes(int32_t(0), int32_zero);
    verify_as_memcmp_bytes(std::numeric_limits<int32_t>::max(), int32_max);
    verify_as_memcmp_bytes(UINT32_C(0x12345678), uint32_value);
    verify_as_memcmp_bytes(char16_t(0x1234), char16_value);

#if BOOST_ENDIAN_LITTLE_BYTE
    uint32_t big_endian_value = UINT32_C(0x12345678);
    MemcmpTestOutput big_endian_output;
    big_endian_output << as_big_endian(big_endian_value);
    TERARK_VERIFY_EQ(big_endian_output.data.size(), sizeof(uint32_t));
    TERARK_VERIFY_F(
        memcmp(big_endian_output.data.data(), uint32_value,
               sizeof(uint32_t)) == 0,
        "as_big_endian encoded bytes mismatch");
#endif

    const std::vector<unsigned char> malformed_bool = {2};
    MemcmpTestInput malformed_input(malformed_bool);
    bool decoded_bool = false;
    bool rejected = false;
    try {
        malformed_input >> as_memcmp(decoded_bool);
    }
    catch (const DataFormatException&) {
        rejected = true;
    }
    TERARK_VERIFY(rejected);
}

void test_as_memcmp_strings() {
    const std::string values[] = {
        std::string(),
        std::string("\0", 1),
        std::string("\0\0", 2),
        std::string("\0a", 2),
        std::string("a", 1),
        std::string("a\0", 2),
        std::string("aa", 2),
        std::string("\x7F", 1),
        std::string("\x80", 1),
        std::string("\xFF", 1),
    };

    std::vector<unsigned char> previous;
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        const std::vector<unsigned char> encoded =
            encode_with_as_memcmp(values[i]);
        if (i != 0) {
            const size_t common = previous.size() < encoded.size()
                                ? previous.size() : encoded.size();
            const int prefix_order = memcmp(previous.data(), encoded.data(), common);
            TERARK_VERIFY_F(prefix_order < 0 ||
                            (prefix_order == 0 && previous.size() < encoded.size()),
                            "as_memcmp string order mismatch at %zd", i);
        }

        std::string decoded;
        MemcmpTestInput input(encoded);
        input >> as_memcmp(decoded);
        TERARK_VERIFY_EQ(input.tell(), encoded.size());
        TERARK_VERIFY_S_EQ(decoded, values[i]);
        previous = encoded;
    }

    const std::string embedded_zero("a\0b", 3);
    const unsigned char expected[] = {'a', 0, 1, 'b', 0, 0};
    verify_as_memcmp_bytes(embedded_zero, expected);

    const std::string long_plain_string(4096, 'x');
    MemcmpTestOutput batched_output;
    batched_output << as_memcmp(long_plain_string);
    TERARK_VERIFY_EQ(batched_output.write_byte_calls, 0);
    TERARK_VERIFY_EQ(batched_output.ensure_write_calls, 2);
    std::string decoded_long_plain_string;
    MemcmpTestInput long_plain_input(batched_output.data);
    long_plain_input >> as_memcmp(decoded_long_plain_string);
    TERARK_VERIFY_S_EQ(decoded_long_plain_string, long_plain_string);

    std::string long_escaped_string(600, 'x');
    long_escaped_string[255] = '\0';
    long_escaped_string[511] = '\0';
    const auto long_escaped_encoding = encode_with_as_memcmp(long_escaped_string);
    std::string decoded_long_escaped_string;
    MemcmpTestInput long_escaped_input(long_escaped_encoding);
    long_escaped_input >> as_memcmp(decoded_long_escaped_string);
    TERARK_VERIFY_S_EQ(decoded_long_escaped_string, long_escaped_string);

    std::string reused_string(15, 'X');
    const auto reused_string_encoding = encode_with_as_memcmp(std::string("a"));
    MemcmpTestInput reused_string_input(reused_string_encoding);
    reused_string_input >> as_memcmp(reused_string);
    TERARK_VERIFY_S_EQ(reused_string, "a");
    TERARK_VERIFY_EQ(reused_string.data()[reused_string.size()], '\0');

    const int32_t number = -7;
    const double real = 42.5;
    MemcmpTestOutput output;
    output << as_memcmp(number) << as_memcmp(embedded_zero) << as_memcmp(real);

    int32_t decoded_number = 0;
    std::string decoded_string;
    double decoded_real = 0;
    MemcmpTestInput input(output.data);
    input >> as_memcmp(decoded_number)
          >> as_memcmp(decoded_string)
          >> as_memcmp(decoded_real);
    TERARK_VERIFY_EQ(decoded_number, number);
    TERARK_VERIFY_S_EQ(decoded_string, embedded_zero);
    TERARK_VERIFY_F(memcmp(&decoded_real, &real, sizeof(real)) == 0,
                    "as_memcmp composite double mismatch");
    TERARK_VERIFY_EQ(input.tell(), output.data.size());

    const std::vector<unsigned char> malformed = {'a', 0, 2};
    MemcmpTestInput malformed_input(malformed);
    decoded_string.assign(15, 'X');
    bool rejected = false;
    try {
        malformed_input >> as_memcmp(decoded_string);
    }
    catch (const DataFormatException&) {
        rejected = true;
    }
    TERARK_VERIFY(rejected);
    TERARK_VERIFY_S_EQ(decoded_string, "a");
    TERARK_VERIFY_EQ(decoded_string.data()[decoded_string.size()], '\0');

    const std::vector<unsigned char> truncated_values[] = {
        std::vector<unsigned char>(),
        std::vector<unsigned char>(1, 0),
        std::vector<unsigned char>(1, 'a'),
    };
    const std::string truncated_prefixes[] = {"", "", "a"};
    for (size_t i = 0; i < 3; ++i) {
        std::string truncated_string(15, 'X');
        TruncatedMemcmpTestInput truncated_input(truncated_values[i]);
        rejected = false;
        try {
            truncated_input >> as_memcmp(truncated_string);
        }
        catch (const DataFormatException&) {
            rejected = true;
        }
        TERARK_VERIFY(rejected);
        TERARK_VERIFY_S_EQ(truncated_string, truncated_prefixes[i]);
        TERARK_VERIFY_EQ(
            truncated_string.data()[truncated_string.size()], '\0');
    }
}

void test_as_memcmp_dataio_integration() {
    unsigned char buffer[128];
    const int32_t number = -123456;
    const std::string string("a\0b", 3);
    const double real = -0.0;

    NativeDataOutput<MinMemIO> output(buffer);
    output << as_memcmp(number) << as_memcmp(string) << as_memcmp(real);
    const size_t encoded_size = output.current() - buffer;

    int32_t decoded_number = 0;
    std::string decoded_string;
    double decoded_real = 0;
    NativeDataInput<MinMemIO> input(buffer);
    input >> as_memcmp(decoded_number)
          >> as_memcmp(decoded_string)
          >> as_memcmp(decoded_real);

    TERARK_VERIFY_EQ(decoded_number, number);
    TERARK_VERIFY_S_EQ(decoded_string, string);
    TERARK_VERIFY_F(memcmp(&decoded_real, &real, sizeof(real)) == 0,
                    "as_memcmp DataIO double mismatch");
    TERARK_VERIFY_EQ(size_t(input.current() - buffer), encoded_size);
}

template<class Real, class Uint>
Real real_from_bits(Uint bits) {
    static_assert(sizeof(Real) == sizeof(Uint), "float and integer sizes differ");
    Real value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

template<class Real, class Uint>
Uint real_to_bits(Real value) {
    static_assert(sizeof(Real) == sizeof(Uint), "float and integer sizes differ");
    Uint bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

template<class Uint>
Uint encoded_bytes_to_uint(const unsigned char* encoded) {
    Uint bits = 0;
    for (size_t i = 0; i < sizeof(Uint); ++i) {
        bits = Uint((bits << 8) | encoded[i]);
    }
    return bits;
}

template<class Real, class Uint>
void verify_encoding_vector(Uint raw_bits, Uint encoded_bits) {
    const Real input = real_from_bits<Real>(raw_bits);
    unsigned char encoded[sizeof(Real)];
    TERARK_VERIFY_EQ(encode_memcmp_real(input, encoded) - encoded, sizeof(Real));
    TERARK_VERIFY_F(encoded_bytes_to_uint<Uint>(encoded) == encoded_bits,
                    "raw=0x%llX actual=0x%llX expected=0x%llX",
                    (unsigned long long)raw_bits,
                    (unsigned long long)encoded_bytes_to_uint<Uint>(encoded),
                    (unsigned long long)encoded_bits);

    Real output;
    TERARK_VERIFY_EQ(decode_memcmp_real(encoded, &output) - encoded, sizeof(Real));
    TERARK_VERIFY_F((real_to_bits<Real, Uint>(output) == raw_bits),
                    "round trip changed 0x%llX to 0x%llX",
                    (unsigned long long)raw_bits,
                    (unsigned long long)real_to_bits<Real, Uint>(output));
}

template<class Real, class Uint, size_t Size>
void verify_total_order(const Uint (&raw_bits)[Size]) {
    unsigned char previous[sizeof(Real)];
    unsigned char current[sizeof(Real)];
    for (size_t i = 0; i < Size; ++i) {
        const Real input = real_from_bits<Real>(raw_bits[i]);
        TERARK_VERIFY_EQ(encode_memcmp_real(input, current) - current, sizeof(Real));
        if (i != 0) {
            TERARK_VERIFY_F(memcmp(previous, current, sizeof(Real)) < 0,
                            "encoded order mismatch: 0x%llX should precede 0x%llX",
                            (unsigned long long)raw_bits[i - 1],
                            (unsigned long long)raw_bits[i]);
        }

        Real output;
        TERARK_VERIFY_EQ(decode_memcmp_real(current, &output) - current,
                         sizeof(Real));
        TERARK_VERIFY_F((real_to_bits<Real, Uint>(output) == raw_bits[i]),
                        "round trip changed 0x%llX to 0x%llX",
                        (unsigned long long)raw_bits[i],
                        (unsigned long long)real_to_bits<Real, Uint>(output));
        memcpy(previous, current, sizeof(previous));
    }
}

template<class Real, class Uint, size_t Size>
void verify_normal_value_order(const Real (&values)[Size]) {
    unsigned char previous[sizeof(Real)];
    unsigned char current[sizeof(Real)];
    for (size_t i = 0; i < Size; ++i) {
        TERARK_VERIFY_F(std::isnormal(values[i]), "%f is not a normal value",
                        values[i]);
        TERARK_VERIFY_EQ(encode_memcmp_real(values[i], current) - current,
                         sizeof(Real));
        if (i != 0) {
            TERARK_VERIFY_F(values[i - 1] < values[i],
                            "%f should precede %f", values[i - 1], values[i]);
            TERARK_VERIFY_F(memcmp(previous, current, sizeof(Real)) < 0,
                            "normal-value encoded order mismatch: %f vs %f",
                            values[i - 1], values[i]);
        }

        Real output;
        TERARK_VERIFY_EQ(decode_memcmp_real(current, &output) - current,
                         sizeof(Real));
        TERARK_VERIFY_EQ((real_to_bits<Real, Uint>(output)),
                         (real_to_bits<Real, Uint>(values[i])));
        memcpy(previous, current, sizeof(previous));
    }
}

uint64_t next_random(uint64_t* state) {
    uint64_t value = *state;
    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

template<class Real, class Uint>
void test_random_round_trip_and_finite_order(uint64_t seed) {
    static const Uint SignBit = Uint(1) << (sizeof(Uint) * 8 - 1);
    uint64_t state = seed;
    for (size_t i = 0; i < 10000; ++i) {
        const Uint a_bits = Uint(next_random(&state));
        const Uint b_bits = Uint(next_random(&state));
        const Real a = real_from_bits<Real>(a_bits);
        const Real b = real_from_bits<Real>(b_bits);
        unsigned char a_encoded[sizeof(Real)];
        unsigned char b_encoded[sizeof(Real)];
        encode_memcmp_real(a, a_encoded);
        encode_memcmp_real(b, b_encoded);

        Real a_output;
        Real b_output;
        decode_memcmp_real(a_encoded, &a_output);
        decode_memcmp_real(b_encoded, &b_output);
        TERARK_VERIFY_EQ((real_to_bits<Real, Uint>(a_output)), a_bits);
        TERARK_VERIFY_EQ((real_to_bits<Real, Uint>(b_output)), b_bits);

        if (isnan(a) || isnan(b)) {
            continue;
        }

        const int actual = memcmp(a_encoded, b_encoded, sizeof(Real));
        int expected = 0;
        if (a < b) {
            expected = -1;
        }
        else if (a > b) {
            expected = 1;
        }
        else if (a_bits != b_bits) {
            // The only distinct, non-NaN bit patterns that compare equal are
            // -0 and +0. IEEE 754 totalOrder places -0 before +0.
            expected = a_bits & SignBit ? -1 : 1;
        }
        TERARK_VERIFY_F((actual < 0) == (expected < 0) &&
                        (actual > 0) == (expected > 0),
                        "numeric/memcmp order mismatch: 0x%llX vs 0x%llX",
                        (unsigned long long)a_bits,
                        (unsigned long long)b_bits);
    }
}

template<class Real>
void test_scaled_finite_order(Real a1, Real b1) { // a < b
    unsigned char am[sizeof(Real)];
    unsigned char bm[sizeof(Real)];
    for (size_t i = 0; i < 100; i++) {
        TERARK_VERIFY_EQ(encode_memcmp_real(a1, am) - am, sizeof(Real));
        TERARK_VERIFY_EQ(encode_memcmp_real(b1, bm) - bm, sizeof(Real));
        TERARK_VERIFY_F(memcmp(am, bm, sizeof(Real)) < 0, "%f %f", a1, b1);
        Real a2, b2;
        TERARK_VERIFY_EQ(decode_memcmp_real(am, &a2) - am, sizeof(Real));
        TERARK_VERIFY_EQ(decode_memcmp_real(bm, &b2) - bm, sizeof(Real));
        TERARK_VERIFY_F(memcmp(&a1, &a2, sizeof(Real)) == 0, "%f %f", a1, a2);
        TERARK_VERIFY_F(memcmp(&b1, &b2, sizeof(Real)) == 0, "%f %f", b1, b2);
        a1 *= 1.05;
        b1 *= 1.05;
    }
}

template<class Real>
void test_scaled_finite_values() {
    test_scaled_finite_order<Real>(+0.00, +0.01);
    test_scaled_finite_order<Real>(-0.01, +0.00);
    test_scaled_finite_order<Real>(+0.10, +0.11);
    test_scaled_finite_order<Real>(+0.10, +0.21);
    test_scaled_finite_order<Real>(-0.11, -0.10);
    test_scaled_finite_order<Real>(-0.21, -0.10);
}

void test_foundationdb_float_encoding() {
    verify_encoding_vector<float>(UINT32_C(0xFFC00001), UINT32_C(0x003FFFFE));
    verify_encoding_vector<float>(UINT32_C(0xFF800000), UINT32_C(0x007FFFFF));
    verify_encoding_vector<float>(UINT32_C(0xC0000000), UINT32_C(0x3FFFFFFF));
    verify_encoding_vector<float>(UINT32_C(0xBF800000), UINT32_C(0x407FFFFF));
    verify_encoding_vector<float>(UINT32_C(0xBF000000), UINT32_C(0x40FFFFFF));
    verify_encoding_vector<float>(UINT32_C(0x80000000), UINT32_C(0x7FFFFFFF));
    verify_encoding_vector<float>(UINT32_C(0x00000000), UINT32_C(0x80000000));
    verify_encoding_vector<float>(UINT32_C(0x3F000000), UINT32_C(0xBF000000));
    verify_encoding_vector<float>(UINT32_C(0x3F800000), UINT32_C(0xBF800000));
    verify_encoding_vector<float>(UINT32_C(0x40000000), UINT32_C(0xC0000000));
    verify_encoding_vector<float>(UINT32_C(0x42F70000), UINT32_C(0xC2F70000));
    verify_encoding_vector<float>(UINT32_C(0x7F800000), UINT32_C(0xFF800000));
    verify_encoding_vector<float>(UINT32_C(0x7FC00001), UINT32_C(0xFFC00001));

    const float normal_values[] = {
        -123.5F, -2.0F, -1.0F, -0.5F,
          +0.5F, +1.0F, +2.0F, +123.5F,
    };
    verify_normal_value_order<float, uint32_t>(normal_values);

    const uint32_t ordered[] = {
        UINT32_C(0xFFC00001), // negative quiet NaN
        UINT32_C(0xFF800001), // negative signaling NaN
        UINT32_C(0xFF800000), // negative infinity
        UINT32_C(0xFF7FFFFF), // lowest finite value
        UINT32_C(0xBF800000), // -1
        UINT32_C(0x80000001), // negative minimum subnormal
        UINT32_C(0x80000000), // -0
        UINT32_C(0x00000000), // +0
        UINT32_C(0x00000001), // positive minimum subnormal
        UINT32_C(0x3F800000), // +1
        UINT32_C(0x7F7FFFFF), // largest finite value
        UINT32_C(0x7F800000), // positive infinity
        UINT32_C(0x7F800001), // positive signaling NaN
        UINT32_C(0x7FC00001), // positive quiet NaN
    };
    verify_total_order<float>(ordered);
    test_random_round_trip_and_finite_order<float, uint32_t>(UINT64_C(0x123456789ABCDEF0));
}

void test_foundationdb_double_encoding() {
    verify_encoding_vector<double>(UINT64_C(0xFFF8000000000001), UINT64_C(0x0007FFFFFFFFFFFE));
    verify_encoding_vector<double>(UINT64_C(0xFFF0000000000000), UINT64_C(0x000FFFFFFFFFFFFF));
    verify_encoding_vector<double>(UINT64_C(0xC000000000000000), UINT64_C(0x3FFFFFFFFFFFFFFF));
    verify_encoding_vector<double>(UINT64_C(0xBFF0000000000000), UINT64_C(0x400FFFFFFFFFFFFF));
    verify_encoding_vector<double>(UINT64_C(0xBFE0000000000000), UINT64_C(0x401FFFFFFFFFFFFF));
    verify_encoding_vector<double>(UINT64_C(0x8000000000000000), UINT64_C(0x7FFFFFFFFFFFFFFF));
    verify_encoding_vector<double>(UINT64_C(0x0000000000000000), UINT64_C(0x8000000000000000));
    verify_encoding_vector<double>(UINT64_C(0x3FE0000000000000), UINT64_C(0xBFE0000000000000));
    verify_encoding_vector<double>(UINT64_C(0x3FF0000000000000), UINT64_C(0xBFF0000000000000));
    verify_encoding_vector<double>(UINT64_C(0x4000000000000000), UINT64_C(0xC000000000000000));
    verify_encoding_vector<double>(UINT64_C(0x405EE00000000000), UINT64_C(0xC05EE00000000000));
    verify_encoding_vector<double>(UINT64_C(0x7FF0000000000000), UINT64_C(0xFFF0000000000000));
    verify_encoding_vector<double>(UINT64_C(0x7FF8000000000001), UINT64_C(0xFFF8000000000001));

    const double normal_values[] = {
        -123.5, -2.0, -1.0, -0.5,
          +0.5, +1.0, +2.0, +123.5,
    };
    verify_normal_value_order<double, uint64_t>(normal_values);

    const uint64_t ordered[] = {
        UINT64_C(0xFFF8000000000001), // negative quiet NaN
        UINT64_C(0xFFF0000000000001), // negative signaling NaN
        UINT64_C(0xFFF0000000000000), // negative infinity
        UINT64_C(0xFFEFFFFFFFFFFFFF), // lowest finite value
        UINT64_C(0xBFF0000000000000), // -1
        UINT64_C(0x8000000000000001), // negative minimum subnormal
        UINT64_C(0x8000000000000000), // -0
        UINT64_C(0x0000000000000000), // +0
        UINT64_C(0x0000000000000001), // positive minimum subnormal
        UINT64_C(0x3FF0000000000000), // +1
        UINT64_C(0x7FEFFFFFFFFFFFFF), // largest finite value
        UINT64_C(0x7FF0000000000000), // positive infinity
        UINT64_C(0x7FF0000000000001), // positive signaling NaN
        UINT64_C(0x7FF8000000000001), // positive quiet NaN
    };
    verify_total_order<double>(ordered);
    test_random_round_trip_and_finite_order<double, uint64_t>(UINT64_C(0x0FEDCBA987654321));
}

void test_str_coding(fstring str, fstring enc) {
    char* e_buf = (char*)alloca(2*str.n + 2);
    char* d_buf = (char*)alloca(2*str.n + 2);
    char* e_end = e_buf + 2*str.n + 2;
    char* e_ptr = encode_0_01_00(str.begin(), str.end(), e_buf, e_end);
    TERARK_VERIFY_EQ(enc.n, e_ptr - e_buf);
    TERARK_VERIFY_S_EQ(enc, fstring(e_buf, e_ptr));
    TERARK_VERIFY_EQ(end_of_01_00(e_buf) - e_buf, enc.n);
    const char* e_ptr2 = nullptr;
    char* d_ptr = decode_01_00(e_buf, &e_ptr2, d_buf, d_buf + enc.n);
    TERARK_VERIFY_EQ(e_ptr2 - e_buf, enc.n);
    TERARK_VERIFY_S_EQ(str, fstring(d_buf, d_ptr));
}

int main(int, char* argv[]) {
    static_assert(sizeof(float) == sizeof(uint32_t), "requires IEEE binary32");
    static_assert(sizeof(double) == sizeof(uint64_t), "requires IEEE binary64");

    test_str_coding("a", {"a\0\0", 3});
    test_str_coding({"a\0", 2}, {"a\0\1\0\0", 5});
    test_str_coding({"\0a\0", 3}, {"\0\1a\0\1\0\0", 7});
    test_scaled_finite_values<float>();
    test_scaled_finite_values<double>();
    test_foundationdb_float_encoding();
    test_foundationdb_double_encoding();
    test_as_memcmp_numbers();
    test_as_memcmp_strings();
    test_as_memcmp_dataio_integration();
    printf("%s passed\n", argv[0]);
	return 0;
}
