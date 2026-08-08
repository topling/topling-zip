#include <cmath>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <terark/util/memcmp_coding.hpp>
#include <terark/util/function.hpp>
#include <terark/stdtypes.hpp>
#include <terark/fstring.hpp>

using namespace terark;

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

        if (std::isnan(a) || std::isnan(b)) {
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
    printf("%s passed\n", argv[0]);
	return 0;
}
