#pragma once

#include <terark/config.hpp>
#include <terark/node_layout.hpp>
#include <terark/stdtypes.hpp>
#include <terark/bits_rotate.hpp>
#include <algorithm>
#include <functional>

// HSM_ : Hash String Map
#define HSM_SANITY assert

namespace terark {

template<class UintHash, class UintVal>
inline UintHash terark_warn_unused_result
FaboHashCombine(UintHash h0, UintVal val) {
	BOOST_STATIC_ASSERT(sizeof(UintVal) <= sizeof(UintHash));
	return BitsRotateLeft(h0, 5) + val;
}

TERARK_DLL_EXPORT size_t __hsm_stl_next_prime(size_t __n);

inline size_t __hsm_align_pow2(size_t x) {
	assert(x > 0);
	size_t p = 0, y = x;
	while (y)
		y >>= 1, ++p;
	if ((size_t(1) << (p-1)) == x)
		return x;
	else
		return size_t(1) << p;
}

namespace hash_common {

	template<bool Test, class Then, class Else>
	struct IF { typedef Then type; };
	template<class Then, class Else>
	struct IF<false, Then, Else> { typedef Else type; };

	template<int Bits> struct AllOne {
		typedef typename IF<(Bits <= 32), uint32_t, uint64_t>::type Uint;
		static const Uint value = (Uint(1) << Bits) - 1;
	};
	template<> struct AllOne<32> {
		typedef uint32_t Uint;
		static const Uint value = Uint(-1);
	};
	template<> struct AllOne<64> {
		typedef uint64_t Uint;
		static const Uint value = Uint(-1);
	};
} // hash_common

template<class Uint, int Bits = sizeof(Uint) * 8>
struct TERARK_DLL_EXPORT dummy_bucket {
	typedef Uint link_t;
	static const Uint tail = terark::hash_common::AllOne<Bits>::value;
	static const Uint delmark = tail-1;
	static const Uint maxlink = tail-2;
};

template<class Key, class Hash = std::hash<Key>, class Equal = std::equal_to<Key> >
struct hash_and_equal : private Hash, private Equal {
	size_t hash(const Key& x) const { return Hash::operator()(x); }
	bool  equal(const Key& x, const Key& y) const {
	   	return Equal::operator()(x, y);
   	}

	template<class OtherKey>
	size_t hash(const OtherKey& x) const { return Hash::operator()(x); }

	template<class Key1, class Key2>
	bool  equal(const Key1& x, const Key2& y) const {
	   	return Equal::operator()(x, y);
   	}

	hash_and_equal() {}
	hash_and_equal(const Hash& h, const Equal& eq) : Hash(h), Equal(eq) {}
};

struct HSM_DefaultDeleter { // delete NULL is OK
	template<class T> void operator()(T* p) const { delete p; }
};

template<class HashMap, class ValuePtr, class Deleter>
class terark_ptr_hash_map : private Deleter {
	HashMap map;
//	typedef void (type_safe_bool*)(terark_ptr_hash_map**, Deleter**);
//	static void type_safe_bool_true(terark_ptr_hash_map**, Deleter**) {}
public:
	typedef typename HashMap::  key_type   key_type;
	typedef ValuePtr value_type;
//	BOOST_STATIC_ASSERT(boost::mpl::is_pointer<value_type>);

	~terark_ptr_hash_map() { del_all(); }

	value_type operator[](const key_type& key) const {
		size_t idx = map.find_i(key);
		return idx == map.end_i() ? NULL : map.val(idx);
	}

	bool is_null(const key_type& key) const { return (*this)[key] == NULL; }

	void replace(const key_type& key, value_type pval) {
		std::pair<size_t, bool> ib = map.insert_i(key, pval);
		if (!ib.second) {
			value_type& old = map.val(ib.first);
			if (old) static_cast<Deleter&>(*this)(old);
			old = pval;
		}
	}
	std::pair<value_type*, bool> insert(const key_type& key, value_type pval) {
		std::pair<size_t, bool> ib = map.insert_i(key, pval);
		return std::make_pair(&map.val(ib.first), ib.second);
	}
	void clear() {
		del_all();
		map.clear();
	}
	void erase_all() {
		del_all();
	   	map.erase_all();
	}
	size_t erase(const key_type& key) {
		size_t idx = map.find_i(key);
		if (map.end_i() == idx) {
			return 0;
		} else {
			value_type& pval = map.val(idx);
			if (pval) {
				static_cast<Deleter&>(*this)(pval);
				pval = NULL;
			}
			map.erase_i(idx);
			return 1;
		}
	}
	size_t size() const { return map.size(); }
	bool  empty() const { return map.empty(); }

	template<class OP>
	void for_each(OP op) const { map.template for_each<OP>(op); }
	template<class OP>
	void for_each(OP op)       { map.template for_each<OP>(op); }

	HashMap& get_map() { return map; }
	const HashMap& get_map() const { return map; }

private:
	void del_all() {
		if (map.delcnt() == 0) {
			for (size_t i = 0; i < map.end_i(); ++i) {
				value_type& p = map.val(i);
				if (p) { static_cast<Deleter&>(*this)(p); p = NULL; }
			}
		} else {
			for (size_t i = map.beg_i(); i < map.end_i(); i = map.next_i(i)) {
				value_type& p = map.val(i);
				if (p) { static_cast<Deleter&>(*this)(p); p = NULL; }
			}
		}
	}
};

} // namespace terark
