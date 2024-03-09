#include <terark/fstring.hpp>
#include <terark/util/function.hpp>
#include <map>

#include "debug_check_malloc.hpp"
#include <terark/sso.hpp>

int main() {
    using namespace terark;
    minimal_sso<32> sso("abc");
    assert(sso.is_local());
    TERARK_ASSERT_S_EQ(sso, "abc");

    sso.assign("");
    assert(sso.is_local());
    assert(sso.empty());
    TERARK_ASSERT_EQ(sso.size(), 0);
    TERARK_ASSERT_S_EQ(sso, "");

    sso.append("0123456789");
    assert(sso.is_local());
    TERARK_ASSERT_S_EQ(sso, "0123456789");
    sso.append("0123456789");
    sso.append("0123456789");
    assert(sso.is_local());
    TERARK_ASSERT_S_EQ(sso, "012345678901234567890123456789");
    sso.append("0");
    assert(sso.is_local());
    TERARK_ASSERT_EQ(sso.size(), 31);
    TERARK_ASSERT_S_EQ(sso, "0123456789012345678901234567890");
    sso.append("1");
    assert(!sso.is_local());
    TERARK_ASSERT_EQ(sso.size(), 32);
    TERARK_ASSERT_S_EQ(sso, "01234567890123456789012345678901");

    sso.assign("0123456789012345678901234567890123456789");
    assert(!sso.is_local());
    TERARK_ASSERT_EQ(sso.size(), 40);
    TERARK_ASSERT_S_EQ(sso, "0123456789012345678901234567890123456789");

    sso.assign("0123456789012345678901234567890");
    assert(!sso.is_local());
    TERARK_ASSERT_EQ(sso.size(), 31);
    TERARK_ASSERT_S_EQ(sso, "0123456789012345678901234567890");

    sso.shrink_to_fit();
    assert(sso.is_local());
    assert(sso.is_local_full());
    TERARK_ASSERT_EQ(sso.size(), 31);
    TERARK_ASSERT_S_EQ(sso, "0123456789012345678901234567890");

    sso.reserve(32);
    assert(!sso.is_local());
    TERARK_ASSERT_EQ(sso.size(), 31);
    TERARK_ASSERT_S_EQ(sso, "0123456789012345678901234567890");
    size_t cap = sso.capacity();
    TERARK_ASSERT_GE(cap, 32);
    sso.push_back('1');
    TERARK_ASSERT_EQ(sso.size(), 32);
    TERARK_ASSERT_EQ(cap, sso.capacity());
    TERARK_ASSERT_S_EQ(sso, "01234567890123456789012345678901");

    sso.pop_back();
    assert(!sso.is_local());
    TERARK_ASSERT_EQ(sso.size(), 31);
    TERARK_ASSERT_EQ(cap, sso.capacity());
    TERARK_ASSERT_S_EQ(sso, "0123456789012345678901234567890");
    sso.shrink_to_fit();
    assert(sso.is_local());
    assert(sso.is_local_full());
    TERARK_ASSERT_EQ(sso.size(), 31);
    TERARK_ASSERT_S_EQ(sso, "0123456789012345678901234567890");

    sso.pop_back();
    assert(sso.is_local());
    assert(!sso.is_local_full());
    TERARK_ASSERT_EQ(sso.size(), 30);
    TERARK_ASSERT_S_EQ(sso, "012345678901234567890123456789");

    sso.resize(70, '7');
    assert(!sso.is_local());
    assert(!sso.empty());
    TERARK_ASSERT_EQ(sso.size(), 70);
    TERARK_ASSERT_S_EQ(sso, "0123456789012345678901234567897777777777777777777777777777777777777777");

    sso.resize(20);
    assert(!sso.is_local());
    assert(!sso.empty());
    TERARK_ASSERT_EQ(sso.size(), 20);
    TERARK_ASSERT_S_EQ(sso, "01234567890123456789");

    sso.shrink_to_fit();
    assert(sso.is_local());
    TERARK_ASSERT_EQ(sso.size(), 20);
    TERARK_ASSERT_S_EQ(sso, "01234567890123456789");

    sso += "0123456789";
    assert(sso.is_local());
    TERARK_ASSERT_EQ(sso.size(), 30);
    TERARK_ASSERT_S_EQ(sso, "012345678901234567890123456789");

    sso.resize(20);
    sso = sso + "0123456789";
    assert(sso.is_local());
    TERARK_ASSERT_EQ(sso.size(), 30);
    TERARK_ASSERT_S_EQ(sso, "012345678901234567890123456789");

    sso.resize(20);
    sso = "0123456789" + sso;
    assert(sso.is_local());
    TERARK_ASSERT_EQ(sso.size(), 30);
    TERARK_ASSERT_S_EQ(sso, "012345678901234567890123456789");

    sso.clear();
    assert(sso.is_local());
    assert(sso.empty());
    TERARK_ASSERT_EQ(sso.size(), 0);
    TERARK_ASSERT_S_EQ(sso, "");

    sso.reserve(32);
    assert(!sso.is_local());
    assert(sso.empty());
    TERARK_ASSERT_EQ(sso.size(), 0);
    TERARK_ASSERT_S_EQ(sso, "");

    sso.clear();
    assert(!sso.is_local());
    assert(sso.empty());
    TERARK_ASSERT_EQ(sso.size(), 0);
    TERARK_ASSERT_S_EQ(sso, "");

    return 0;
}
