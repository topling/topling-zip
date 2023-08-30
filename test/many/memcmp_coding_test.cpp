#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <terark/util/memcmp_coding.hpp>
#include <terark/util/function.hpp>
#include <terark/stdtypes.hpp>
#include <terark/fstring.hpp>

using namespace terark;

template<class Real>
void do_test(Real a1, Real b1) { // a < b
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
void test() {
    do_test<Real>(+0.00, +0.01);
    do_test<Real>(-0.01, +0.00);
    do_test<Real>(+0.10, +0.11);
    do_test<Real>(+0.10, +0.21);
    do_test<Real>(-0.11, -0.10);
    do_test<Real>(-0.21, -0.10);
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

int main(int argc, char* argv[]) {
    test_str_coding("a", {"a\0\0", 3});
    test_str_coding({"a\0", 2}, {"a\0\1\0\0", 5});
    test_str_coding({"\0a\0", 3}, {"\0\1a\0\1\0\0", 7});
    test<float>();
    test<double>();
    printf("%s passed\n", argv[0]);
	return 0;
}
