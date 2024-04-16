#include <terark/fstring.hpp>
#include <terark/util/function.hpp>
#include <map>

#if !defined(NDEBUG)
#include "debug_check_malloc.hpp"
#endif
#include <terark/sso.hpp>

int main(int argc, char* argv[]) {
    using namespace terark;
    minimal_sso<32> sso("abc");
    assert(sso.is_local());
    TERARK_VERIFY_S_EQ(sso, "abc");

    sso.assign("");
    assert(sso.is_local());
    assert(sso.empty());
    TERARK_VERIFY_EQ(sso.size(), 0);
    TERARK_VERIFY_S_EQ(sso, "");

    sso.append("0123456789");
    assert(sso.is_local());
    TERARK_VERIFY_S_EQ(sso, "0123456789");
    sso.append("0123456789");
    sso.append("0123456789");
    assert(sso.is_local());
    TERARK_VERIFY_S_EQ(sso, "012345678901234567890123456789");
    sso.append("0");
    assert(sso.is_local());
    TERARK_VERIFY_EQ(sso.size(), 31);
    TERARK_VERIFY_S_EQ(sso, "0123456789012345678901234567890");
    sso.append("1");
    assert(!sso.is_local());
    TERARK_VERIFY_EQ(sso.size(), 32);
    TERARK_VERIFY_S_EQ(sso, "01234567890123456789012345678901");

    sso.assign("0123456789012345678901234567890123456789");
    assert(!sso.is_local());
    TERARK_VERIFY_EQ(sso.size(), 40);
    TERARK_VERIFY_S_EQ(sso, "0123456789012345678901234567890123456789");

    sso.assign("0123456789012345678901234567890");
    assert(!sso.is_local());
    TERARK_VERIFY_EQ(sso.size(), 31);
    TERARK_VERIFY_S_EQ(sso, "0123456789012345678901234567890");

    sso.shrink_to_fit();
    assert(sso.is_local());
    assert(sso.is_local_full());
    TERARK_VERIFY_EQ(sso.size(), 31);
    TERARK_VERIFY_S_EQ(sso, "0123456789012345678901234567890");

    sso.reserve(32);
    assert(!sso.is_local());
    TERARK_VERIFY_EQ(sso.size(), 31);
    TERARK_VERIFY_S_EQ(sso, "0123456789012345678901234567890");
    size_t cap = sso.capacity();
    TERARK_VERIFY_GE(cap, 32);
    sso.push_back('1');
    TERARK_VERIFY_EQ(sso.size(), 32);
    TERARK_VERIFY_EQ(cap, sso.capacity());
    TERARK_VERIFY_S_EQ(sso, "01234567890123456789012345678901");

    sso.pop_back();
    assert(!sso.is_local());
    TERARK_VERIFY_EQ(sso.size(), 31);
    TERARK_VERIFY_EQ(cap, sso.capacity());
    TERARK_VERIFY_S_EQ(sso, "0123456789012345678901234567890");
    sso.shrink_to_fit();
    assert(sso.is_local());
    assert(sso.is_local_full());
    TERARK_VERIFY_EQ(sso.size(), 31);
    TERARK_VERIFY_S_EQ(sso, "0123456789012345678901234567890");

    sso.pop_back();
    assert(sso.is_local());
    assert(!sso.is_local_full());
    TERARK_VERIFY_EQ(sso.size(), 30);
    TERARK_VERIFY_S_EQ(sso, "012345678901234567890123456789");

    sso.resize(70, '7');
    assert(!sso.is_local());
    assert(!sso.empty());
    TERARK_VERIFY_EQ(sso.size(), 70);
    TERARK_VERIFY_S_EQ(sso, "0123456789012345678901234567897777777777777777777777777777777777777777");

    sso.resize(20);
    assert(!sso.is_local());
    assert(!sso.empty());
    TERARK_VERIFY_EQ(sso.size(), 20);
    TERARK_VERIFY_S_EQ(sso, "01234567890123456789");

    sso.shrink_to_fit();
    assert(sso.is_local());
    TERARK_VERIFY_EQ(sso.size(), 20);
    TERARK_VERIFY_S_EQ(sso, "01234567890123456789");

    sso += "0123456789";
    assert(sso.is_local());
    TERARK_VERIFY_EQ(sso.size(), 30);
    TERARK_VERIFY_S_EQ(sso, "012345678901234567890123456789");

    sso.resize(20);
    sso = sso + "0123456789";
    assert(sso.is_local());
    TERARK_VERIFY_EQ(sso.size(), 30);
    TERARK_VERIFY_S_EQ(sso, "012345678901234567890123456789");

    sso.resize(20);
    sso = "0123456789" + sso;
    assert(sso.is_local());
    TERARK_VERIFY_EQ(sso.size(), 30);
    TERARK_VERIFY_S_EQ(sso, "012345678901234567890123456789");

    sso.clear();
    sso.assign(fstring("0123456789"));
    sso.append(fstring("0123456789"));
    sso.append(fstring("0123456789"));
    assert(sso.is_local());
    TERARK_VERIFY_EQ(sso.size(), 30);
    TERARK_VERIFY_S_EQ(sso, "012345678901234567890123456789");
    TERARK_VERIFY_S_EQ(sso.substr(5), "5678901234567890123456789");
    TERARK_VERIFY_S_EQ(sso.substr(5, 5), "56789");
    TERARK_VERIFY_S_EQ(sso.prefix<fstring>(5), "01234");
    TERARK_VERIFY_S_EQ(sso.suffix<fstring>(5), "56789");
    TERARK_VERIFY_S_EQ(sso.notail<fstring>(5), "0123456789012345678901234");

    sso.assign(argv[0], strlen(argv[0])-1);
    sso.destroy();
    assert(sso.is_local());
    assert(sso.empty());
    TERARK_VERIFY_EQ(sso.size(), 0);
    TERARK_VERIFY_S_EQ(sso, "");

    sso.reserve(32);
    assert(!sso.is_local());
    assert(sso.empty());
    TERARK_VERIFY_EQ(sso.size(), 0);
    TERARK_VERIFY_S_EQ(sso, "");

    sso.clear();
    assert(!sso.is_local());
    assert(sso.empty());
    TERARK_VERIFY_EQ(sso.size(), 0);
    TERARK_VERIFY_S_EQ(sso, "");

    sso.assign("0123456789012345678901234567890123456789");
    assert(!sso.is_local());
    TERARK_VERIFY_S_EQ(sso.substr(5), "56789012345678901234567890123456789");
    TERARK_VERIFY_S_EQ(sso.substr(5, 5), "56789");
    TERARK_VERIFY_S_EQ(sso.prefix<fstring>(5), "01234");
    TERARK_VERIFY_S_EQ(sso.suffix<fstring>(5), "56789");
    TERARK_VERIFY_S_EQ(sso.notail<fstring>(5), "01234567890123456789012345678901234");
    sso.destroy();
    assert(sso.is_local());
    assert(sso.empty());
    TERARK_VERIFY_EQ(sso.size(), 0);
    TERARK_VERIFY_S_EQ(sso, "");

    auto sso1 = decltype(sso)("0123456789012345678901234567890123456789");
    assert(!sso1.is_local());
    TERARK_VERIFY_S_EQ(sso1, "0123456789012345678901234567890123456789");

    auto sso2 = decltype(sso)("0123456789012345678901234567890");
    assert(sso2.is_local());
    TERARK_VERIFY_S_EQ(sso2, "0123456789012345678901234567890");

    std::swap(sso1, sso2);
    assert(sso1.is_local());
    TERARK_VERIFY_S_EQ(sso1, "0123456789012345678901234567890");
    assert(!sso2.is_local());
    TERARK_VERIFY_S_EQ(sso2, "0123456789012345678901234567890123456789");

    return 0;
}
