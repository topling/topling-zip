#pragma once

#include <string>
#include <type_traits>

#include <terark/config.hpp>
#include "hash_common.hpp"
#include <boost/predef/other/endian.h>
#include <terark/util/byte_swap_impl.hpp>
#include <terark/parallel_lib.hpp>
#include <terark/fstring.hpp>

#if !defined(BOOST_ENDIAN_BIG_BYTE) && !defined(BOOST_ENDIAN_LITTLE_BYTE)
# error must define byte endian
#endif

#if defined(__GNUC__) && __GNUC_MINOR__ + 1000 * __GNUC__ > 7000
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wclass-memaccess" // which version support?
#endif

// http://software.intel.com/en-us/articles/memcpy-performance
//  This link mentioned memcpy may be optimized by hardware, Whether aligned or unaligned:
//    hardware implementations which automatically switch into wide move modes
//    would be more attractive than software support
#define HSM_ENABLE_MY_memcpy

namespace terark {
	template<class LinkTp>
	struct KeyIndexWithPrefix_tt {
		LinkTp offset;
		LinkTp length;
		LinkTp prefix; // string prefix, for optimize CPU Cache
		LinkTp idx; // index to pNodes
	};

	template<class LinkTp, class LinkT2, class Value>
	struct NodeWithPrefix_tt {
		LinkTp offset;
		LinkTp length;
		LinkT2 prefix; // string prefix, for optimize CPU Cache
		Value  value;
	};
} // namespace terark

namespace std {
	template<class LinkTp>
	inline void swap(terark::KeyIndexWithPrefix_tt<LinkTp>& x,
					 terark::KeyIndexWithPrefix_tt<LinkTp>& y)
	{
		std::swap(x.offset, y.offset);
		std::swap(x.length, y.length);
		std::swap(x.prefix, y.prefix);
		std::swap(x.idx, y.idx);
	}
	template<class LinkTp, class LinkT2, class Value>
	inline void swap(terark::NodeWithPrefix_tt<LinkTp, LinkT2, Value>& x,
					 terark::NodeWithPrefix_tt<LinkTp, LinkT2, Value>& y)
	{
		std::swap(x.offset, y.offset);
		std::swap(x.length, y.length);
		std::swap(x.prefix, y.prefix);
		std::swap(x.value, y.value);
	//	boost::swap(x.value, y.value); // Arguments Dependent Lookup
	}
} // namespace std

namespace terark {

struct fstring_hash_equal
  : hash_and_equal<fstring, fstring_func::hash, fstring_func::equal> {};
struct fstring_hash_equal_align
  : hash_and_equal<fstring, fstring_func::hash_align, fstring_func::equal_align> {};
struct fstring_hash_equal_unalign
  : hash_and_equal<fstring, fstring_func::hash_unalign, fstring_func::equal_unalign> {};

struct ByteWiseKeyCompare {
	const char* ps;
	template<class TWithPrefix>
	bool operator()(const TWithPrefix& x, const TWithPrefix& y) const {
		enum { prefix_len = sizeof(x.prefix) };
		if (x.prefix < y.prefix) return true;
		if (y.prefix < x.prefix) return false;
		if (x.length <= prefix_len || y.length <= prefix_len)
				return x.length < y.length;
		fstring sx(ps+LOAD_OFFSET(x.offset)+prefix_len, x.length-prefix_len);
		fstring sy(ps+LOAD_OFFSET(y.offset)+prefix_len, y.length-prefix_len);
		return fstring_func::IF_SP_ALIGN(less_align, less_unalign)()(sx, sy);
	}
	ByteWiseKeyCompare(const char* ps) : ps(ps) {}
};

// LinkTp is also offset-type
template<class LinkTp, class Value, class ValuePlace>
struct hash_strmap_node;
template<class LinkTp, class Value>
struct hash_strmap_node<LinkTp, Value, ValueOut> {
	LinkTp offset; // offset to string pool
	LinkTp link;   // link
};
template<class LinkTp, class Value>
struct hash_strmap_node<LinkTp, Value, ValueInline> {
	LinkTp offset; // offset to string pool
	LinkTp link;   // link
	Value  value;
};

template<class Value, class ValuePlace>
class hash_strmap_ValuesPtr;
template<class Value> class hash_strmap_ValuesPtr<Value, ValueInline> {
protected:
	static constexpr Value* values = nullptr;
	void set_values_ptr(Value*) {}
	void swap(hash_strmap_ValuesPtr&) {}
};
template<class Value> class hash_strmap_ValuesPtr<Value, ValueOut> {
protected:
	Value* values = nullptr;
	void set_values_ptr(Value* v) { values = v; }
	void swap(hash_strmap_ValuesPtr& y) { std::swap(values, y.values); }
};

#if !defined(HSM_FreeListWithDebug)
	#if defined(NDEBUG)
		#define HSM_FreeListWithDebug 0
	#else
		#define HSM_FreeListWithDebug 1
	#endif
#endif

template<class LinkTp, bool>
class hash_strmap_FreeList {
protected:
	struct FreeList {
		LinkTp  head = dummy_bucket<LinkTp>::tail;
	  #if HSM_FreeListWithDebug
		LinkTp  llen = 0; ///< list length
		size_t  freq = 0;
	  #endif
	};
	FreeList* fastlist = nullptr;
};
template<class LinkTp>
class hash_strmap_FreeList<LinkTp, false> {
protected:
	struct FreeList {};
	static constexpr FreeList* fastlist = nullptr;
};

//
// hash_strmap<> hset; // just a set, can be used as a string pool
//
template< class Value = ValueOut // ValueOut means empty value, just like a set
		, class HashFunc = fstring_func::IF_SP_ALIGN(hash_align, hash)
		, class KeyEqual = fstring_func::IF_SP_ALIGN(equal_align, equal)
		, class ValuePlace = typename boost::mpl::if_c<
					!boost::is_empty<Value>::value && // ValueInline should be non-empty
					sizeof(Value) % sizeof(intptr_t) == 0 && sizeof(Value) <= 32
				, ValueInline
				, ValueOut // If Value is empty, ValueOut will not use memory
			>::type
		, class CopyStrategy = FastCopy
		, class LinkTp = unsigned int // could be unsigned short for small map
		, class HashTp = HSM_HashTp
		, bool WithFreeList = !CopyStrategy::is_fast_copy || std::is_same_v<ValuePlace, ValueOut>
		    // WithFreeList true: free list can be enabled or disabled,
		    // WithFreeList default equal to 'SlowCopy or ValueOut'
		>
class hash_strmap : dummy_bucket<LinkTp>, HashFunc, KeyEqual
                  , hash_strmap_ValuesPtr<Value, ValuePlace>
                  , hash_strmap_FreeList<LinkTp, WithFreeList>
{
protected:
	using hash_strmap_ValuesPtr<Value, ValuePlace>::values;
	struct ValueRaw {
		char block[sizeof(Value)];
	};
	typedef hash_strmap_node<LinkTp, Value, ValuePlace> Node;

	using dummy_bucket<LinkTp>::tail;
	using dummy_bucket<LinkTp>::delmark; // tail-1
	using dummy_bucket<LinkTp>::maxlink; // tail-2, all link must < maxlink
	static const size_t maxoffset =	tail * size_t(sizeof(LinkTp) < sizeof(size_t) ? SP_ALIGN : 1);
	static const intptr_t hash_cache_disabled = 8; // good if compiler aggressive optimize

	Node*   pNodes;
	LinkTp  nNodes;   // hard limit is maxlink, even in 64bit platform
	LinkTp  maxNodes; // hard limit is maxlink
	LinkTp  maxload;  // hard limit is maxlink, guard for calling rehash
	LinkTp  nDeleted;
  #if defined(HSM_ENABLE_HASH_CACHE)
	HashTp* pHash;   // array of cached hash value
  #endif

	LinkTp* bucket;  // index to pNodes
	size_t  nBucket; // could be larger up to 4G-1 when size_t is 32 bits
                     // and more when size_t is 64 bits

	char*    strpool;
	LinkTp   lenpool;
	LinkTp   maxpool;  // hard limit is SAVE_OFFSET(maxoffset)

    struct FreeLink {
        LinkTp  next;
    };
	// When WithFreeList is true, FreeLink must be fit into SP_ALIGN,
	// this is very unlikely to happen, but we need to ensure it!
	static_assert(!WithFreeList || sizeof(FreeLink) <= SP_ALIGN);
	typedef typename hash_strmap_FreeList<LinkTp, WithFreeList>::FreeList FreeList;
	using            hash_strmap_FreeList<LinkTp, WithFreeList>::fastlist;
	static const short freelist_disabled = -1;

	LinkTp  freepool; // LinkTp is uint32 for most cases
	short   fastleng;
	byte_t  load_factor; // real load factor = load_factor / 256
	enum sort_type : unsigned char {
		en_unsorted,
		en_sort_by_key,
		en_sort_by_val
	} sort_flag;

    template<class T>
    T* t_malloc(size_t count) {
        return (T*)malloc(sizeof(T) * count);
    }
    template<class T>
    T* t_realloc(T* p, size_t count) {
        return (T*)realloc(p, sizeof(T) * count);
    }

    void init() {
		pNodes = NULL;
		nNodes = 0;
		maxNodes = 0;
		maxload = 0;
		nDeleted = 0;
	  #if defined(HSM_ENABLE_HASH_CACHE)
		pHash = NULL; // default enabled
	  #endif

		bucket = const_cast<LinkTp*>(&tail); // false start
		nBucket = 1;

		strpool = NULL;
		lenpool = 0;
		maxpool = 0;
		freepool = 0;

		this->set_values_ptr(NULL);
		fastleng = freelist_disabled;
		if constexpr (WithFreeList) {
			fastlist = NULL;
		}

		load_factor = byte_t(256.0 * 0.3);
		sort_flag = en_unsorted;
	}

	Value& nth_value(size_t nth, ValueInline) const { return pNodes[nth].value; }
	Value& nth_value(size_t nth, ValueOut   ) const { return values[nth]; }
	Value& nth_value(size_t nth) const { return nth_value(nth, ValuePlace()); }

	Value& nth_value(Node* pn, Value*, size_t nth, ValueInline) const { return pn[nth].value; }
	Value& nth_value(Node*, Value* pv, size_t nth, ValueOut   ) const { return pv[nth]; }
	Value& nth_value(Node* pn, Value* pv, size_t nth) const {
        return nth_value(pn, pv, nth, ValuePlace());
    }

public:
	static size_t extralen(const char* ps, size_t End) { return ps[End-1] + 1; }
	size_t extralen(size_t End) const { return strpool[End-1] + 1; }

	HashTp hash(fstring x) const { return static_cast<const HashFunc&>(*this)(x); }
	bool  equal(fstring x, fstring y) const { return static_cast<const KeyEqual&>(*this)(x, y); }

private:
	void move_value_on_raw(Node*, Node*, Value* vdest, Value* vsrc, ValueOut) {
		CopyStrategy::move_cons(vdest, *vsrc);
	}
	void move_value_on_raw(Node* dest, Node* src, Value*, Value*, ValueInline) {
		CopyStrategy::move_cons(&dest->value, src->value);
	}
    void move_value_on_raw(Node* dest, Node* src, Value* vdest, Value* vsrc) {
        move_value_on_raw(dest, src, vdest, vsrc, ValuePlace());
    }

	size_t allnodes_size(size_t n) const {
		// guard offset used for compute strlen of pNodes[nNodes-1]
		// guard link   used for speedup next_i
		size_t s = sizeof(Node) * n + sizeof(pNodes->offset) + sizeof(pNodes->link);
		return (s + 15) & (size_t)ptrdiff_t(~15); // align to 16
	}

	void reserve_nodes_impl(size_t cap, SafeCopy, ValueInline) {
		Node* pn = (Node*)malloc(allnodes_size(cap));
		if (NULL == pn) TERARK_DIE("malloc(%zd)", allnodes_size(cap));
		if (pNodes) {
			Node* oldpn = pNodes;
			if (nDeleted) {
				for (size_t i = 0, n = nNodes; i < n; ++i) {
                    pn[i].offset = oldpn[i].offset;
                    pn[i].link = oldpn[i].link;
					if (delmark != oldpn[i].link)
                        CopyStrategy::move_cons(&pn[i].value, oldpn[i].value);
                }
			} else {
				for (size_t i = 0, n = nNodes; i < n; ++i) {
                    pn[i].offset = oldpn[i].offset;
                    pn[i].link = oldpn[i].link;
                    CopyStrategy::move_cons(&pn[i].value, oldpn[i].value);
                }
			}
			free(oldpn);
		}
		pn[nNodes].offset = lenpool;
		pn[nNodes].link = tail; // guard for next_i
		pNodes = pn;
	}
	void reserve_nodes_impl(size_t cap, SafeCopy, ValueOut) {
		Node* pn = (Node*)realloc(pNodes, allnodes_size(cap));
		if (NULL == pn) TERARK_DIE("realloc(%zd)", allnodes_size(cap));
		if (!is_value_empty) {
			Value* pv = (Value*)malloc(sizeof(Value) * cap);
			if (NULL == pv) TERARK_DIE("malloc(%zd)", sizeof(Value) * cap);
		  if constexpr (ValuePlace::is_value_out && !is_value_empty) {
			if (values) {
				Value* oldpv = values;
				if (nDeleted) {
					for (size_t i = 0, n = nNodes; i < n; ++i)
						if (delmark != pn[i].link)
                            CopyStrategy::move_cons(pv+i, oldpv[i]);
				} else {
					for (size_t i = 0, n = nNodes; i < n; ++i)
                        CopyStrategy::move_cons(pv+i, oldpv[i]);
				}
				free(oldpv);
			}
			values = pv;
		  }
		}
		if (NULL == pNodes) {
			pn[0].offset = 0;
			pn[0].link = tail; // guard for next_i
		}
		pNodes = pn;
	}
	// This is the normal case:
	void reserve_nodes_impl(size_t cap, FastCopy, ValueInline) {
		Node* pn = (Node*)realloc(pNodes, allnodes_size(cap));
		if (NULL == pn) TERARK_DIE("realloc(%zd)", allnodes_size(cap));
		if (NULL == pNodes) {
			pn[0].offset = 0;
			pn[0].link = tail; // guard for next_i
		}
		pNodes = pn;
	}
	void reserve_nodes_impl(size_t cap, FastCopy, ValueOut) {
		Node* pn = (Node*)realloc(pNodes, allnodes_size(cap));
		if (NULL == pn) TERARK_DIE("realloc(%zd)", allnodes_size(cap));
		if constexpr (!is_value_empty) {
			Value* pv = (Value*)realloc(values, sizeof(Value) * cap);
			if (NULL == pv) {
				pNodes = pn; // maxNodes unchanged
				TERARK_DIE("realloc(%zd)", sizeof(Value) * cap);
			}
			values = pv;
		}
		if (NULL == pNodes) {
			pn[0].offset = 0;
			pn[0].link = tail; // guard for next_i
		}
		pNodes = pn;
	}

	void relink_impl(bool bFillHash) {
	//	TERARK_ASSERT_EQ(0, nDeleted);
    //  TERARK_ASSERT_EQ(0, freepool);
		LinkTp* pb = bucket;
		Node*   pn = pNodes;
		size_t  nb = nBucket;

		// (LinkTp)tail is just used for coerce tail be passed by value
		// otherwise, tail is passed by reference, this cause g++ link error
		std::fill_n(pb, nb, (LinkTp)tail);

	  #if defined(HSM_ENABLE_HASH_CACHE)
		// nothing
	  #else
		static const int pHash = hash_cache_disabled;
		TERARK_UNUSED_VAR(bFillHash);
	  #endif
		if (intptr_t(pHash) == hash_cache_disabled) {
		  if (0 == nDeleted)
			for (size_t j = 0, n = nNodes; j < n; ++j) {
				size_t ib = size_t(hash(key(j)) % nb);
				pn[j].link = pb[ib];
				pb[ib] = LinkTp(j);
			}
		  else
			for (size_t j = 0, n = nNodes; j < n; ++j) {
				if (delmark != pn[j].link) {
					size_t ib = size_t(hash(key(j)) % nb);
					pn[j].link = pb[ib];
					pb[ib] = LinkTp(j);
				}
			}
		}
	  #if defined(HSM_ENABLE_HASH_CACHE)
		else if (bFillHash) {
			HashTp* ph = pHash;
		  if (0 == nDeleted)
			for (size_t j = 0, n = nNodes; j < n; ++j) {
                HashTp hh = hash(key(j));
				size_t ib = size_t(hh % nb);
				pn[j].link = pb[ib];
                ph[j] = hh;
				pb[ib] = LinkTp(j);
			}
		  else
			for (size_t j = 0, n = nNodes; j < n; ++j) {
				if (delmark != pn[j].link) {
					HashTp hh = hash(key(j));
					size_t ib = size_t(hh % nb);
					pn[j].link = pb[ib];
					ph[j] = hh;
					pb[ib] = LinkTp(j);
				}
			}
		}
        else {
			const HashTp* ph = pHash;
		  if (0 == nDeleted)
			for (size_t j = 0, n = nNodes; j < n; ++j) {
				size_t ib = size_t(ph[j] % nb);
				pn[j].link = pb[ib];
				pb[ib] = LinkTp(j);
			}
		  else
			for (size_t j = 0, n = nNodes; j < n; ++j) {
				if (delmark != pn[j].link) {
					size_t ib = size_t(ph[j] % nb);
					pn[j].link = pb[ib];
					pb[ib] = LinkTp(j);
				}
			}
        }
	  #endif
	}

	void relink() { relink_impl(false); }
	void relink_fill() { relink_impl(true); }

	void destroy() {
        if constexpr (WithFreeList)
            free(fastlist);
		if (pNodes) {
			if (!boost::has_trivial_destructor<Value>::value) {
				for (size_t i = nNodes; i > 0; --i)
					if (pNodes[i-1].link != delmark)
						nth_value(i-1).~Value();
			}
			free(pNodes);
		}
	  #if defined(HSM_ENABLE_HASH_CACHE)
		if (pHash && intptr_t(pHash) != hash_cache_disabled)
			free(pHash);
	  #endif
		if (bucket && &tail != bucket)
			free(bucket);
		if (strpool)
			free(strpool);
		if constexpr (ValuePlace::is_value_out && !is_value_empty)
			free(values);
	}

public:
	class iterator {
	public:
		typedef Value		  mapped_type;
		typedef hash_strmap*  OwnerPtr;
	#define ClassIterator iterator
	#include "hash_strmap_iterator.hpp"
	};
	class const_iterator {
	public:
		const_iterator(const iterator& y)
		  : owner(y.get_owner()), index(y.get_index()) {}
		typedef const Value         mapped_type;
		typedef const hash_strmap*  OwnerPtr;
	#define ClassIterator const_iterator
	#include "hash_strmap_iterator.hpp"
	};

	typedef std::reverse_iterator<iterator> reverse_iterator;
	typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

	typedef typename iterator::difference_type difference_type;
	typedef typename iterator::size_type  size_type;
	typedef typename iterator::value_type value_type;
	typedef typename iterator::reference  reference;

	typedef const fstring key_type;
	typedef Value mapped_type;
	typedef LinkTp link_t;

	static const bool is_value_empty = boost::is_empty<Value>::value;
	typedef boost::mpl::bool_< // CPU cache line is often 64
		(ValuePlace::is_value_out && !is_value_empty) || (sizeof(Node) > 64)
	> sort_by_index;
	typedef boost::mpl::false_ sort_by_index_no;
	typedef boost::mpl::true_  sort_by_index_yes;

public:
	hash_strmap() { init(); }
	explicit hash_strmap(HashFunc hashfn, KeyEqual equalfn = KeyEqual())
	  : HashFunc(hashfn), KeyEqual(equalfn) {
		init();
	}
	explicit hash_strmap(size_t cap, HashFunc hashfn = HashFunc(), KeyEqual equalfn = KeyEqual())
	  : HashFunc(hashfn), KeyEqual(equalfn) {
		init();
		reserve(cap);
	}
	hash_strmap(std::initializer_list<fstring> list,
				HashFunc hashfn = HashFunc(), KeyEqual equalfn = KeyEqual())
	  : HashFunc(hashfn), KeyEqual(equalfn) {
		init();
		reserve(list.size());
		for (fstring key : list)
			this->insert_i(key);
	}
	hash_strmap(std::initializer_list<std::pair<fstring, Value> > list,
				HashFunc hashfn = HashFunc(), KeyEqual equalfn = KeyEqual())
	  : HashFunc(hashfn), KeyEqual(equalfn) {
		init();
		reserve(list.size());
		for (const auto& kv : list)
			this->insert_i(kv.first, kv.second);
	}
	hash_strmap(const hash_strmap& y)
	  : HashFunc(y), KeyEqual(y) {
		pNodes = NULL;
		nNodes = freelist_disabled==y.fastleng ? y.nNodes-y.nDeleted : y.nNodes;
		maxNodes = y.nNodes;
		maxload = y.maxload;
		nBucket = y.nBucket;
		nDeleted = 0;
	  #if defined(HSM_ENABLE_HASH_CACHE)
		pHash = NULL;
	  #endif

		strpool = NULL;
		lenpool = y.lenpool;
		maxpool = y.lenpool;
		freepool = y.freepool;

		load_factor = y.load_factor;
		sort_flag = y.sort_flag;
		this->set_values_ptr(NULL);

		if constexpr (WithFreeList) {
			fastleng = y.fastleng;
			fastlist = NULL;
			if (0 == nNodes && freelist_disabled != fastleng) {
				size_t bytes = sizeof(FreeList) * (fastleng+1);
				fastlist = (FreeList*)malloc(bytes);
				if (nullptr == fastlist) {
					TERARK_DIE("malloc(fastleng %d, bytes %zd)", fastleng, bytes);
				}
				std::uninitialized_fill_n(fastlist, fastleng+1, FreeList());
			}
		}

		if (0 == nNodes) { // empty
			nBucket = 1;
			maxload = 0;
			bucket  = const_cast<LinkTp*>(&tail);
		  #if defined(HSM_ENABLE_HASH_CACHE)
			if (intptr_t(y.pHash) == hash_cache_disabled)
				pHash = (HashTp*)(hash_cache_disabled);
		  #endif
			return; // not need to do deep copy
		}
		bucket = (LinkTp*)malloc(sizeof(LinkTp) * y.nBucket);
		if (NULL == bucket) {
			TERARK_DIE("malloc(%zd)", sizeof(LinkTp) * y.nBucket);
		}
		pNodes = (Node*)malloc(allnodes_size(nNodes));
		if (NULL == pNodes) {
			free(bucket);
			init(); // reset to safe state
			TERARK_DIE("malloc(%zd)", allnodes_size(nNodes));
		}
	  #if defined(HSM_ENABLE_HASH_CACHE)
		if (intptr_t(y.pHash) == hash_cache_disabled) {
			pHash = (HashTp*)(hash_cache_disabled);
		}
		else {
			pHash = (HashTp*)malloc(sizeof(HashTp) * nNodes);
			if (NULL == pHash) {
				free(pNodes);
				free(bucket);
				init(); // reset to safe state
				TERARK_DIE("malloc(%zd)", sizeof(HashTp) * nNodes);
			}
		}
	  #endif
		strpool = (char*)malloc(LOAD_OFFSET(y.lenpool));
		if (NULL == strpool) {
		  #if defined(HSM_ENABLE_HASH_CACHE)
			if (intptr_t(pHash) != hash_cache_disabled) free(pHash);
		  #endif
			free(pNodes);
			free(bucket);
			init(); // reset to safe state
			TERARK_DIE("malloc(%zd)", LOAD_OFFSET(y.lenpool));
		}
		if constexpr (ValuePlace::is_value_out && !is_value_empty) {
			values = (Value*)malloc(sizeof(Value) * nNodes);
			if (NULL == values) {
			  #if defined(HSM_ENABLE_HASH_CACHE)
				if (intptr_t(pHash) != hash_cache_disabled) free(pHash);
			  #endif
				free(strpool);
				free(pNodes);
				free(bucket);
				init(); // reset to safe state
				TERARK_DIE("malloc(%zd)", sizeof(Value) * nNodes);
			}
		}
		if constexpr (WithFreeList) {
		if (freelist_disabled != fastleng) {
			fastlist = (FreeList*)malloc(sizeof(FreeList) * (fastleng+1));
			if (NULL == fastlist) {
			  if constexpr (ValuePlace::is_value_out && !is_value_empty) {
				if (NULL != values) free(values);
			  }
			  #if defined(HSM_ENABLE_HASH_CACHE)
				if (intptr_t(pHash) != hash_cache_disabled) free(pHash);
			  #endif
				free(strpool);
				free(pNodes);
				free(bucket);
				init(); // reset to safe state
				TERARK_DIE("malloc(%zd)", sizeof(FreeList) * (fastleng+1));
			}
			memcpy(fastlist, y.fastlist, sizeof(FreeList) * (fastleng+1));
		}
		}
		if (0 == y.nDeleted || freelist_disabled != fastleng) {
		  #if defined(HSM_ENABLE_HASH_CACHE)
			if (intptr_t(pHash) != hash_cache_disabled)
				memcpy(pHash, y.pHash, sizeof(HashTp) * nNodes);
		  #endif
			memcpy(strpool, y.strpool, sizeof(char) * LOAD_OFFSET(lenpool));
			memcpy(bucket , y.bucket , sizeof(LinkTp) * nBucket);
			if (0 == y.nDeleted || CopyStrategy::is_fast_copy) {
				std::uninitialized_copy(y.pNodes, y.pNodes + nNodes, pNodes);
				if constexpr (ValuePlace::is_value_out && !is_value_empty)
					std::uninitialized_copy(y.values, y.values + nNodes, values);
			}
			else {
				for (size_t i = 0, n = y.nNodes; i < n; ++i) {
					pNodes[i].offset = y.pNodes[i].offset;
					pNodes[i].link = y.pNodes[i].link;
					if (delmark != y.pNodes[i].link)
						   new(&nth_value(i))Value(y.nth_value(i));
				}
			}
			pNodes[nNodes].offset = lenpool;
			pNodes[nNodes].link = tail;
			return;
		}
		size_t j = 0;
		// could compiler don't generate code the try ... catch if
		// it would not throw exceptions by static analysis?
		try {
			const Node* ypn = y.pNodes;
			size_t n = y.nNodes;
			size_t loffset = 0;
			for (size_t i = 0; i < n; ++i) {
				if (ypn[i].link != delmark) {
					pNodes[j].offset = SAVE_OFFSET(loffset);
				  #if defined(HSM_ENABLE_HASH_CACHE)
					if (intptr_t(pHash) != hash_cache_disabled)
						pHash[j] = y.pHash[i];
				  #endif
					new(&nth_value(j))Value(y.nth_value(i));
					size_t beg2 = LOAD_OFFSET(ypn[i+0].offset);
					size_t end2 = LOAD_OFFSET(ypn[i+1].offset);
					size_t len2 = end2 - beg2;
					memcpy(strpool + loffset, y.strpool + beg2, len2);
					loffset += len2;
					++j;
				}
			}
			TERARK_VERIFY_EQ(j, nNodes);
			pNodes[j].offset = SAVE_OFFSET(loffset);
			pNodes[j].link = tail;
			lenpool = SAVE_OFFSET(loffset);
			relink();
		}
		catch (...) {
			if (!boost::has_trivial_destructor<Value>::value) {
				while (j > 0) // roll back
					nth_value(j-1).~Value(), --j;
			}
			if constexpr (ValuePlace::is_value_out && !is_value_empty) {
				TERARK_VERIFY(NULL != values);
				free(values);
			}
			free(bucket);
			free(pNodes);
			free(strpool);
			init(); // reset to safe state
			throw;
		}
	}
	hash_strmap& operator=(const hash_strmap& y) {
		if (this != &y) {
		#if 1 // prefer performance, std::map is in this manner
		// this is exception-safe but lose old content of *this on exception
			this->destroy();
			new(this)hash_strmap(y);
		#else // exception-safe, but take more peak memory
			hash_strmap(y).swap(*this);
		#endif
		}
		return *this;
	}

	hash_strmap(hash_strmap&& y)
	  : HashFunc(std::move(y)), KeyEqual(std::move(y)) {
		pNodes   =  y.pNodes;
		nNodes   =  y.nNodes;
		maxNodes =  y.maxNodes;
		maxload  =  y.maxload;
		nDeleted =  y.nDeleted;
	  #if defined(HSM_ENABLE_HASH_CACHE)
		pHash    =  y.pHash;
	  #endif

		nBucket  =  y.nBucket;
		bucket   =  y.bucket;

		strpool  =  y.strpool;
		lenpool  =  y.lenpool;
		maxpool  =  y.maxpool;
		freepool =  y.freepool;

		if constexpr (ValuePlace::is_value_out && !is_value_empty) {
			values   =  y.values;
		}
		if constexpr (WithFreeList) {
			fastleng =  y.fastleng;
			fastlist =  y.fastlist;
		}

		load_factor =  y.load_factor;
		sort_flag =  y.sort_flag;

		y.init(); // reset y as empty
	}
	hash_strmap& operator=(hash_strmap&& y) {
		if (this != &y) {
			this->~hash_strmap();
			new(this)hash_strmap(std::move(y));
		}
		return *this;
	}

	~hash_strmap() { destroy(); }

	void swap(hash_strmap& y) {
		std::swap(pNodes  , y.pNodes);
		std::swap(nNodes  , y.nNodes);
		std::swap(maxNodes, y.maxNodes);
		std::swap(maxload , y.maxload);
		std::swap(nDeleted, y.nDeleted);
	  #if defined(HSM_ENABLE_HASH_CACHE)
		std::swap(pHash   , y.pHash);
	  #endif

		std::swap(nBucket , y.nBucket);
		std::swap(bucket  , y.bucket);

		std::swap(strpool , y.strpool);
		std::swap(lenpool , y.lenpool);
		std::swap(maxpool , y.maxpool);
		std::swap(freepool, y.freepool);

		hash_strmap_ValuesPtr<Value, ValuePlace>::swap(y);
		if constexpr (WithFreeList)	{
			std::swap(fastleng, y.fastleng);
			std::swap(fastlist, y.fastlist);
		}

		std::swap(load_factor, y.load_factor);
		std::swap(sort_flag, y.sort_flag);
		std::swap(static_cast<HashFunc&>(*this), static_cast<HashFunc&>(y));
		std::swap(static_cast<KeyEqual&>(*this), static_cast<KeyEqual&>(y));
	}

	fstring whole_strpool() const { return fstring(strpool, LOAD_OFFSET(lenpool)); }

	size_t capacity() const { return maxNodes; }

	void clear() {
		destroy();
		init();
	}

	// erase all key-values, but keep memory, don't free
	void erase_all() {
		if constexpr (WithFreeList) {
			if (nDeleted && fastlist) {
				TERARK_VERIFY_NE(freelist_disabled, fastleng);
				std::fill_n(fastlist, fastleng+1, FreeList());
			}
		}
		if (nNodes > nDeleted) {
			if (!boost::has_trivial_destructor<Value>::value) {
				if (nDeleted) {
					for (size_t i = 0; i < nNodes; ++i)
						if (delmark != pNodes[i].link)
							nth_value(i).~Value();
				} else {
					for (size_t i = 0; i < nNodes; ++i)
						nth_value(i).~Value();
				}
			}
			TERARK_VERIFY_NE(const_cast<LinkTp*>(&tail), bucket);
			std::fill_n(bucket, nBucket, (LinkTp)tail);
		}
		nDeleted = 0;
		nNodes = 0;
		lenpool = 0;
		freepool = 0;
	}

	void shrink_to_fit() {
		// before calling this function,
		//   the class-invariant(pHash/pLink relation) may not satisfied
		//   such as in this->erase_if..
		if (nDeleted)
			revoke_deleted_no_relink();
		if (0 == nNodes) {
			clear();
			return;
		}
		if (maxpool != lenpool) {
			strpool = (char*)realloc(strpool, LOAD_OFFSET(lenpool)); // never fail
			maxpool = lenpool;
		}
		reserve_nodes(nNodes);
		if (NULL != bucket)
			rehash(size_t(nNodes * 256.0 / load_factor + 1));
	}

	// very risky, you must know what you are doing
	void risk_steal_key_val_and_clear(char** pool, Value** vals) {
		BOOST_STATIC_ASSERT(ValuePlace::is_value_out); // must be ValueOut
		TERARK_VERIFY(NULL != pNodes);
		::free(pNodes);
		if (bucket && bucket != &tail)
			::free(bucket);
	  #if defined(HSM_ENABLE_HASH_CACHE)
		if (pHash && intptr_t(pHash) != hash_cache_disabled)
			::free(pHash);
	  #endif
		*pool = strpool;
		*vals = values;
		init();
	}

	// Keep required memory for iteration, free all other memory
	// after calling this function, just iteration function could be called
	void risk_disable_hash() {
		shrink_to_fit();
		// just keep strpool + data + pNodes
		if (bucket) {
			if (bucket != &tail)
				::free(bucket);
			bucket = NULL;
		}
	  #if defined(HSM_ENABLE_HASH_CACHE)
	//	disable_hash_cache(); // ? OR:
		if (intptr_t(pHash) == hash_cache_disabled)
		{}
		else if (NULL != pHash)
			::free(pHash), pHash = NULL;
	  #endif
	//	if (pLink) // has no pLink
	//		::free(pLink), pLink = NULL;
	}

	void risk_enable_hash() {
		if (NULL == bucket)
			bucket = &tail;
	  #if defined(HSM_ENABLE_HASH_CACHE)
		if (NULL == pHash) {
			pHash = hash_cache_disabled;
			enable_hash_cache();
		}
	  #endif
		rehash(size_t(nNodes * 256.0 / load_factor + 1));
	}

	size_t rehash(size_t newBucketSize) {
		TERARK_VERIFY(NULL != bucket);
		newBucketSize = __hsm_stl_next_prime(newBucketSize);
		TERARK_VERIFY_GE(newBucketSize, 5);
		if (newBucketSize != nBucket) {
			// shrink or enlarge
			if (nBucket != 1) {
				free(bucket);
			} else {
				TERARK_VERIFY_EQ(tail, *bucket);
			}
			bucket = (LinkTp*)malloc(sizeof(LinkTp) * newBucketSize);
			if (NULL == bucket) { // how to handle?
				TERARK_DIE("malloc(%zd) fail", sizeof(LinkTp) * newBucketSize);
			}
			nBucket = newBucketSize;
			relink();
			using namespace std;
			maxload = LinkTp(min<double>(newBucketSize/256.0 * load_factor, maxlink));
		}
		return newBucketSize;
	}
//	void resize(size_t n) { rehash(n); }

	// with rehash
	void reserve(size_t cap) {
		TERARK_VERIFY_GE(cap, nNodes);
		if (cap > maxlink)
			cap = maxlink;
		reserve_nodes(cap);
		rehash(size_t(cap * 256.0 / load_factor + 1));
	}

	// with rehash
	void reserve(size_t cap, size_t poolcap) {
		TERARK_VERIFY_GE(cap, nNodes);
		TERARK_VERIFY_GE(poolcap, LOAD_OFFSET(lenpool));
// commented, because strpool realloc is cheap, Value realloc may expense,
// reserve more Value space may get benefit
//		if (poolcap < IF_SP_ALIGN(LOAD_OFFSET(1), 2) * cap)
//			throw std::logic_error(
//"hash_strmap::reserve(cap,poolcap), poolcap is less than cap expected"
//			);
		if (cap > maxlink)
			cap = maxlink;
		reserve_strpool(poolcap);
		reserve_nodes(cap);
		rehash(size_t(cap * 256.0 / load_factor + 1));
	}

	// just enlarge/shrink strpool
	void reserve_strpool(size_t poolcap) {
		TERARK_VERIFY_GE(poolcap, LOAD_OFFSET(lenpool));
		char* ps = (char*)realloc(strpool, poolcap);
		if (NULL == ps)
			TERARK_DIE("realloc(%zd)", poolcap);
		strpool = ps;
		maxpool = SAVE_OFFSET(poolcap);
	}

	// without rehash
	void reserve_nodes(size_t cap) {
		TERARK_VERIFY_GE(cap, nNodes);
		if (freelist_disabled == fastleng)
			revoke_deleted();
		if (cap > maxlink)
			cap = maxlink;
		if (cap != (size_t)maxNodes) {
		  #if defined(HSM_ENABLE_HASH_CACHE)
			if (intptr_t(pHash) != hash_cache_disabled) {
				HashTp* ph = (HashTp*)realloc(pHash, sizeof(HashTp) * cap);
				if (NULL == ph)
					TERARK_DIE("realloc(%zd)", sizeof(HashTp) * cap);
				pHash = ph;
			}
		  #endif
			reserve_nodes_impl(cap, CopyStrategy(), ValuePlace());
			maxNodes = LinkTp(cap);
		}
	}

	void set_load_factor(double fact) {
		if (fact >= 0.999) {
			throw std::logic_error("load factor must <= 0.999");
		}
		load_factor = std::max<byte_t>(byte_t(fact * 256.0), 1);
		using namespace std;
		maxload = &tail == bucket ? 0
				: LinkTp(min<double>(nBucket * fact, maxlink));
	}
	double get_load_factor() const { return load_factor / 256.0; }

	HashTp hash_value(size_t i) const {
		TERARK_ASSERT_LT(i, nNodes);
	  #if defined(HSM_ENABLE_HASH_CACHE)
		if (intptr_t(pHash) == hash_cache_disabled)
			return hash(key(i));
		else
			return pHash[i];
	  #else
		return hash(key(i));
	  #endif
	}

	bool is_hash_cached() const {
	  #if defined(HSM_ENABLE_HASH_CACHE)
		return intptr_t(pHash) != hash_cache_disabled;
	  #else
		return false;
	  #endif
	}

	void enable_hash_cache() {
	  #if defined(HSM_ENABLE_HASH_CACHE)
		if (intptr_t(pHash) == hash_cache_disabled) {
			if (0 == maxNodes) {
				pHash = NULL;
			}
			else {
				HashTp* ph = (HashTp*)malloc(sizeof(HashTp) * maxNodes);
				if (NULL == ph) {
					TERARK_DIE("malloc(%zd)", sizeof(HashTp) * maxNodes);
				}
				size_t  n = nNodes;
				Node*  pn = pNodes;
				if (0 == nDeleted) {
					for (size_t i = 0; i < n; ++i)
						ph[i] = hash(key(i));
				}
				else {
					for (size_t i = 0; i < n; ++i)
						if (delmark != pn[i].link)
							ph[i] = hash(key(i));
				}
				pHash = ph;
			}
		}
		TERARK_VERIFY(NULL != pHash || 0 == maxNodes);
	  #endif
	}

	void disable_hash_cache() {
	  #if defined(HSM_ENABLE_HASH_CACHE)
		if (intptr_t(pHash) == hash_cache_disabled)
			return;
		if (pHash)
			free(pHash);
		pHash = (HashTp*)(hash_cache_disabled);
	  #endif
	}

	size_t total_key_size() const { return LOAD_OFFSET(lenpool); }
	bool    empty() const { return nNodes== nDeleted; }
	size_t   size() const { return nNodes - nDeleted; }
	size_t  end_i() const { return nNodes; }
	size_t  beg_i() const {
	//	return next_i(intptr_t(-1)); assertion will fail in next_i
	//	because size_t is unsigned, so write the code below
	//	this code produce same result as next_i(intptr_t(-1))
	   	const Node* pn = pNodes;
		if (NULL == pn) return 0;
	   	size_t i = 0;
	   	while (delmark == pn[i].link)
			++i;
		return i;
	}
	size_t rbeg_i() const { return nDeleted == nNodes ? 0 : nNodes; }
	size_t rend_i() const { return 0; }
	size_t delcnt() const { return nDeleted; }

	std::pair<size_t, bool> insert_i(const std::pair<fstring, Value>& kv) {
		return insert_i(kv.first, kv.second);
	}

	Value& operator[](const fstring key) {
		std::pair<size_t, bool> ib = insert_i(key);
		return nth_value(ib.first, ValuePlace());
	}
#if 1
	const Value& operator[](const fstring key) const {
		const size_t i = find_i(key);
		if (i == nNodes) {
			std::string msg;
			msg.append("hash_strmap::operator[] const: key=\"");
			msg.append(key.p, key.n);
			msg.append("\" doesn't exist");
			throw std::logic_error(msg);
		}
		return nth_value(i, ValuePlace());
	}
#endif
	const Value& val(size_t idx) const {
		TERARK_ASSERT_GE(nNodes, 1);
		TERARK_ASSERT_LT(idx, nNodes);
		return nth_value(idx, ValuePlace());
	}
	Value& val(size_t idx) {
		TERARK_ASSERT_GE(nNodes, 1);
		TERARK_ASSERT_LT(idx, nNodes);
		return nth_value(idx, ValuePlace());
	}

	const Value& end_val(size_t idxEnd) const {
		TERARK_ASSERT_GE(nNodes, 1);
		TERARK_ASSERT_LE(idxEnd, nNodes);
		size_t idx = nNodes - idxEnd;
		return nth_value(idx, ValuePlace());
	}
	Value& end_val(size_t idxEnd) {
		TERARK_ASSERT_GE(nNodes, 1);
		TERARK_ASSERT_LE(idxEnd, nNodes);
		size_t idx = nNodes - idxEnd;
		return nth_value(idx, ValuePlace());
	}

	      iterator  begin()       { return       iterator(this, beg_i()); }
	const_iterator  begin() const { return const_iterator(this, beg_i()); }
	const_iterator cbegin() const { return const_iterator(this, beg_i()); }

	      iterator  end()       { return       iterator(this, nNodes); }
	const_iterator  end() const { return const_iterator(this, nNodes); }
	const_iterator cend() const { return const_iterator(this, nNodes); }

	      reverse_iterator  rbegin()       { return       reverse_iterator(end()); }
	const_reverse_iterator  rbegin() const { return const_reverse_iterator(end()); }
	const_reverse_iterator crbegin() const { return const_reverse_iterator(end()); }

	      reverse_iterator  rend()       { return       reverse_iterator(begin()); }
	const_reverse_iterator  rend() const { return const_reverse_iterator(begin()); }
	const_reverse_iterator crend() const { return const_reverse_iterator(begin()); }

	      iterator  iter_of(size_t idx)       { return       iterator(this, idx); }
	const_iterator  iter_of(size_t idx) const { return const_iterator(this, idx); }

	std::pair<iterator, bool>
		insert(const std::pair<std::string, Value>& kv) {
		std::pair<size_t, bool> ib = insert_i(kv);
		return std::pair<iterator, bool>(iterator(this, ib.first), ib.second);
	}
	std::pair<iterator, bool>
	insert(const std::pair<const fstring, Value>& kv) {
		std::pair<size_t, bool> ib = insert_i(kv);
		return std::pair<iterator, bool>(iterator(this, ib.first), ib.second);
	}
	std::pair<iterator, bool>
	insert(const std::pair<const char*, Value>& kv) {
		std::pair<size_t, bool> ib = insert_i(kv);
		return std::pair<iterator, bool>(iterator(this, ib.first), ib.second);
	}
	std::pair<iterator, bool>
	insert(const fstring& k) {
		std::pair<size_t, bool> ib = insert_i(k);
		return std::pair<iterator, bool>(iterator(this, ib.first), ib.second);
	}
	template<class InputIter>
	void insert(InputIter first, InputIter last) {
		for (; first != last; ++first)
			insert_i(first->first, first->second);
	}

	std::pair<iterator, bool>
	emplace(fstring key, const Value& val) {
		std::pair<size_t, bool> ib = insert_i(key, val);
		return std::pair<iterator, bool>(iterator(this, ib.first), ib.second);
	}
	std::pair<iterator, bool>
	emplace(fstring key, Value&& val) {
		std::pair<size_t, bool> ib = insert_i(key, std::move(val));
		return std::pair<iterator, bool>(iterator(this, ib.first), ib.second);
	}

	template<class ValueParam>
	std::pair<iterator, bool>
	try_emplace(fstring key, const ValueParam& vp) {
		std::pair<size_t, bool> ib = lazy_insert_i(key, CopyConsFunc<Value>(vp));
		return std::pair<iterator, bool>(iterator(this, ib.first), ib.second);
	}
	template<class ValueParam>
	std::pair<iterator, bool>
	try_emplace(fstring key, ValueParam&& vp) {
		std::pair<size_t, bool> ib = lazy_insert_i(key, MoveConsFunc<Value>(std::move(vp)));
		return std::pair<iterator, bool>(iterator(this, ib.first), ib.second);
	}

	      iterator find(fstring key)       { return       iterator(this, find_i(key)); }
	const_iterator find(fstring key) const { return const_iterator(this, find_i(key)); }
	      iterator find(const char* key, size_t len)       { return find(fstring(key, len)); }
	const_iterator find(const char* key, size_t len) const { return find(fstring(key, len)); }

	// return erased element count
	size_t erase(const fstring key) {
		TERARK_ASSERT_GE(key.n, 0);
		HashTp h = hash(key);
		size_t i = size_t(h % nBucket);
		LinkTp p, *p_ptr = &bucket[i];
		while ((p = *p_ptr) != tail) {
			const Node* y = &pNodes[p];
			TERARK_ASSERT_LT(p, nNodes);
			TERARK_ASSERT_LT(y[0].offset, y[1].offset);
			TERARK_ASSERT_LE(y[1].offset, maxpool);
		// doesn't need to compare cached hash value, it almost always true
			size_t ybeg = LOAD_OFFSET(y[0].offset);
			size_t yend = LOAD_OFFSET(y[1].offset);
			size_t ylen = yend - ybeg - extralen(yend);
			if (equal(key, fstring(strpool + ybeg, ylen))) {
				*p_ptr = pNodes[p].link; // ulink_impl(p, h);
				risk_slot_free(p);
				return 1;
			}
			p_ptr = &pNodes[p].link;
		}
		return 0;
	}

	// copy of erase(key) with get the erased value
	size_t erase(const fstring key, Value* erased) {
		TERARK_ASSERT_GE(key.n, 0);
		HashTp h = hash(key);
		size_t i = size_t(h % nBucket);
		LinkTp p, *p_ptr = &bucket[i];
		while ((p = *p_ptr) != tail) {
			const Node* y = &pNodes[p];
			TERARK_ASSERT_LT(p, nNodes);
			TERARK_ASSERT_LT(y[0].offset, y[1].offset);
			TERARK_ASSERT_LE(y[1].offset, maxpool);
		// doesn't need to compare cached hash value, it almost always true
			size_t ybeg = LOAD_OFFSET(y[0].offset);
			size_t yend = LOAD_OFFSET(y[1].offset);
			size_t ylen = yend - ybeg - extralen(yend);
			if (equal(key, fstring(strpool + ybeg, ylen))) {
				*erased = std::move(nth_value(p, ValuePlace()));
				*p_ptr = pNodes[p].link; // ulink_impl(p, h);
				risk_slot_free(p);
				return 1;
			}
			p_ptr = &pNodes[p].link;
		}
		return 0;
	}

	void erase(iterator iter) {
		TERARK_ASSERT_EQ(iter.get_owner(), this);
		TERARK_ASSERT_LT(iter.get_index(), nNodes);
		erase_i(iter.get_index());
	}

private:
	template<class Predictor>
	size_t erase_if_kv_impl(Predictor pred) {
	  #if defined(HSM_ENABLE_HASH_CACHE)
		TERARK_VERIFY_F(intptr_t(pHash) == hash_cache_disabled || NULL != pHash, "%p", pHash);
		HashTp*ph = pHash;
	  #endif
		Value* pv = values;
		Node*  pn = pNodes;
		char*  ps = strpool;
		size_t  n = nNodes;
		size_t  i = 0;
		size_t  loffset;
		if (0 == nDeleted) {
			for (; i < n; ++i) {
				TERARK_ASSERT_LT(pn[i+0].offset, pn[i+1].offset);
				TERARK_ASSERT_LE(pn[i+1].offset, maxpool);
				const size_t  mybeg = LOAD_OFFSET(pn[i+0].offset);
				const size_t  myend = LOAD_OFFSET(pn[i+1].offset);
				const size_t  mylen = myend - mybeg;
				const size_t  extra = extralen(myend);
				const fstring mykey(ps + mybeg, mylen-extra);
				Value& myval = nth_value(pn, pv, i); // non-const
				if (pred(mykey, myval))
					goto DoErase1;
			}
			return 0;
		DoErase1:
			nth_value(pn, pv, i).~Value();
			loffset = LOAD_OFFSET(pn[i].offset); // begin of first deleted
			for (size_t j = i+1; j < n; ++j) {
				TERARK_ASSERT_LT(pn[j+0].offset, pn[j+1].offset);
				TERARK_ASSERT_LE(pn[j+1].offset, maxpool);
				const size_t  mybeg = LOAD_OFFSET(pn[j+0].offset);
				const size_t  myend = LOAD_OFFSET(pn[j+1].offset);
				const size_t  mylen = myend - mybeg;
				const size_t  extra = extralen(myend);
				const fstring mykey(ps + mybeg, mylen-extra);
				Value& myval = nth_value(pn, pv, j); // non-const
				if (pred(mykey, myval))
					nth_value(pn, pv, j).~Value(); // destruct
				else {
					pn[i].offset = SAVE_OFFSET(loffset);
					move_value_on_raw(pn+i, pn+j, pv+i, pv+j);
					memcpy(ps + loffset, ps + mybeg, mylen);
				  #if defined(HSM_ENABLE_HASH_CACHE)
					if (intptr_t(ph) != hash_cache_disabled)
						ph[i] = ph[j];
				  #endif
					loffset += mylen;
					++i;
				}
			}
		}
		else { // 0 != nDeleted
			for (; i < n; ++i) {
				TERARK_ASSERT_LT(pn[i+0].offset, pn[i+1].offset);
				TERARK_ASSERT_LE(pn[i+1].offset, maxpool);
				const size_t  mybeg = LOAD_OFFSET(pn[i+0].offset);
				const size_t  myend = LOAD_OFFSET(pn[i+1].offset);
				const size_t  mylen = myend - mybeg;
				const size_t  extra = extralen(myend);
				const fstring mykey(ps + mybeg, mylen-extra);
				Value& myval = nth_value(pn, pv, i); // non-const
				if (delmark == pn[i].link || pred(mykey, myval))
					goto DoErase2;
			}
			return 0;
		DoErase2:
			nth_value(pn, pv, i).~Value();
			loffset = LOAD_OFFSET(pn[i].offset); // begin of first deleted
			for (size_t j = i+1; j < n; ++j) {
				TERARK_ASSERT_LT(pn[j+0].offset, pn[j+1].offset);
				TERARK_ASSERT_LE(pn[j+1].offset, maxpool);
				const size_t  mybeg = LOAD_OFFSET(pn[j+0].offset);
				const size_t  myend = LOAD_OFFSET(pn[j+1].offset);
				const size_t  mylen = myend - mybeg;
				const size_t  extra = extralen(myend);
				const fstring mykey(ps + mybeg, mylen-extra);
				Value& myval = nth_value(pn, pv, j); // non-const
				if (delmark == pn[j].link)
				{} // do nothing
				else if (pred(mykey, myval))
					nth_value(pn, pv, j).~Value(); // destruct
				else {
					pn[i].offset = SAVE_OFFSET(loffset);
					move_value_on_raw(pn+i, pn+j, pv+i, pv+j);
					memcpy(ps + loffset, ps + mybeg, mylen);
				  #if defined(HSM_ENABLE_HASH_CACHE)
					if (intptr_t(ph) != hash_cache_disabled)
						ph[i] = ph[j];
				  #endif
					loffset += mylen;
					++i;
				}
			}
		}
		size_t nDeleted0 = nDeleted;
		lenpool = SAVE_OFFSET(loffset);
		nNodes = LinkTp(i);
		pn[i].offset = SAVE_OFFSET(loffset);
		pn[i].link = tail; // guard for next_i
		nDeleted = 0;
		freepool = 0;
		return n - nDeleted0 - i;
	}
public:
	template<class Predictor>
	size_t erase_if_kv(Predictor pred) {
		if (fastleng == freelist_disabled) {
			size_t nErased = erase_if_kv_impl(pred);
			if (nNodes * 3/2 <= maxNodes)
				shrink_to_fit();
			else
				relink();
			return nErased;
		} else
			return keepid_erase_if_kv(pred);
	}
	template<class Predictor>
	size_t shrink_after_erase_if_kv(Predictor pred) {
		size_t nErased = erase_if_kv_impl(pred);
		shrink_to_fit();
		return nErased;
	}
	template<class Predictor>
	size_t no_shrink_after_erase_if_kv(Predictor pred) {
		size_t nErased = erase_if_kv_impl(pred);
		relink();
		return nErased;
	}
private:
	template<class Predictor>
	struct PPairToKV {
		bool operator()(const fstring& k, Value& v) {
			value_type kv(k, v);
			return pred(kv);
		}
		PPairToKV(Predictor pred_) : pred(pred_) {}
		Predictor pred;
	};
public:
	template<class Predictor>
	size_t erase_if(Predictor pred) {
		return erase_if_kv(PPairToKV<Predictor>(pred));
	}
	template<class Predictor>
	size_t shrink_after_erase_if(Predictor pred) {
		return shrink_after_erase_if_kv(PPairToKV<Predictor>(pred));
	}
	template<class Predictor>
	size_t no_shrink_after_erase_if(Predictor pred) {
		return no_shrink_after_erase_if_kv(PPairToKV<Predictor>(pred));
	}

	template<class Predictor>
	size_t keepid_erase_if_kv(Predictor pred) {
		TERARK_VERIFY_NE(freelist_disabled, fastleng);
	  #if defined(HSM_ENABLE_HASH_CACHE)
		TERARK_VERIFY_F(intptr_t(pHash) == hash_cache_disabled || NULL != pHash, "%p", pHash);
		const HashTp* ph = pHash;
	  #endif
		const char*   ps = strpool;
		const LinkTp   n = nNodes;
		const size_t  nb = nBucket;
		Value* pv = values;
		Node*  pn = pNodes;
		LinkTp nErased = 0;
		for (LinkTp i = 0; i < n; ++i) {
			TERARK_ASSERT_LT(pn[i+0].offset, pn[i+1].offset);
			TERARK_ASSERT_LE(pn[i+1].offset, maxpool);
			const size_t  mybeg = LOAD_OFFSET(pn[i+0].offset);
			const size_t  myend = LOAD_OFFSET(pn[i+1].offset);
			const size_t  mylen = myend - mybeg;
			const size_t  extra = extralen(myend);
			const fstring mykey(ps + mybeg, mylen-extra);
			Value& myval = nth_value(pn, pv, i); // non-const
			if (delmark != pn[i].link && pred(mykey, myval)) {
			  #if defined(HSM_ENABLE_HASH_CACHE)
				HashTp hh = intptr_t(ph) == hash_cache_disabled ? hash(mykey) : ph[i];
			  #else
				HashTp hh = hash(mykey);
			  #endif
				size_t ib = size_t(hh % nb);
				TERARK_ASSERT_NE(tail, bucket[ib]);
				LinkTp* p = &bucket[ib];
				while (i != *p) {
					p = &pn[*p].link;
					TERARK_ASSERT_LT(*p, n);
				}
				*p = pNodes[i].link; // delete the node from link list
				pn[i].link = delmark; // set deletion mark
				myval.~Value();
				put_to_freelist(i);
				nErased++;
				freepool += SAVE_OFFSET(mylen);
			}
		}
		nDeleted += nErased;
		return nErased;
	}
	template<class Predictor>
	size_t keepid_erase_if(Predictor pred) {
		return keepid_erase_if_kv(PPairToKV<Predictor>(pred));
	}

private:
	void revoke_deleted_no_relink() throw() {
        TERARK_VERIFY_EQ(freelist_disabled, fastleng);
		TERARK_VERIFY_GT(nDeleted, 0);
		ptrdiff_t n = nNodes;
		char*  ps = strpool;
		Node*  pn = pNodes;
		Value* pv = values;
	  #if defined(HSM_ENABLE_HASH_CACHE)
		HashTp*ph = pHash;
	  #endif
		ptrdiff_t idx1 = 0;
		for (; idx1 < n; ++idx1) {
			if (pn[idx1].link == delmark)
				break; // first deleted
		}
		size_t loffset = LOAD_OFFSET(pn[idx1].offset); // begin of first deleted
		for (ptrdiff_t idx2 = idx1 + 1; idx2 < n; ++idx2) {
			if (pn[idx2].link != delmark) { // is not deleted
				// string copy should be as fast as possible
			#if SP_ALIGN != 1 && defined(HSM_ENABLE_MY_memcpy)
				size_t beg2 = pn[idx2+0].offset;
				size_t end2 = pn[idx2+1].offset;
				size_t len2 = end2 - beg2; // count by align_type
				TERARK_ASSERT_GT(len2, 0);
				align_type* dst = (align_type*)(ps + loffset);
				align_type* src = (align_type*)(ps + LOAD_OFFSET(beg2));
				while (len2--) *dst++ = *src++;
				len2 = LOAD_OFFSET(end2 - beg2);
			#else
				size_t beg2 = LOAD_OFFSET(pn[idx2+0].offset);
				size_t end2 = LOAD_OFFSET(pn[idx2+1].offset);
				size_t len2 = end2 - beg2;
				memcpy(ps + loffset, ps + beg2, len2);
			#endif
				pn[idx1].offset = SAVE_OFFSET(loffset);
				move_value_on_raw(pn+idx1, pn+idx2, pv+idx1, pv+idx2);
			  #if defined(HSM_ENABLE_HASH_CACHE)
				if (intptr_t(ph) != hash_cache_disabled)
					ph[idx1] = ph[idx2];
			  #endif
			   	loffset += len2;
				++idx1;
			}
		}
		TERARK_VERIFY_EQ(loffset + LOAD_OFFSET(freepool), LOAD_OFFSET(lenpool));
		TERARK_VERIFY_EQ(nNodes  - nDeleted, (LinkTp)idx1);
		lenpool = SAVE_OFFSET(loffset);
		pn[idx1].offset = SAVE_OFFSET(loffset);
		pn[idx1].link = tail; // guard for next_i

		nNodes -= nDeleted;
		nDeleted = 0;
		freepool = 0;
	}

public:
	void revoke_deleted() {
        TERARK_VERIFY_EQ(freelist_disabled, fastleng);
		if (nDeleted) {
			revoke_deleted_no_relink();
			relink();
		}
	}

	size_t next_i(size_t idx) const {
		TERARK_ASSERT_LT(idx, nNodes);
		Node* pn = pNodes;
	#if 0
		// don't use pn[nNodes].link guard
		size_t n = nNodes;
		do ++idx;
		while (idx < n && pn[idx].link == delmark);
	#else
		// pn[nNodes].link is the guard
		do ++idx;
		while (pn[idx].link == delmark);
	#endif
		TERARK_ASSERT_LE(idx, nNodes);
		return idx;
	}

	/**
	A valid reverse iteration by prev_i should be:
	<code>
	for (size_t i = fm.rbeg_i(), re = fm.rend_i(); i != re;) {
		i = fm.prev_i(i);
		cout << fm.key(i) << ',' << fm.val(i) << endl;
	}
	// or by reverse_iterator:
	// reverse_iterator need 2 prev_i call per iteration
	for (auto i = fm.rbegin(); i != fm.rend(); ++i) {
		// using i
	}
	</code>
	*/
	size_t prev_i(size_t idx) const {
		TERARK_ASSERT_GT(idx, 0); // not the begin
		TERARK_ASSERT_LE(idx, nNodes);
		Node* pn = pNodes;
	#if defined(_DEBUG) || !defined(NDEBUG)
		size_t n = nNodes;
		do --idx; // no guard for prev_i, if idx rolled from 0, the loop ended
		while (idx < n && pn[idx].link == delmark);

		// if these 2 assert fail,
		//    it should caused by iterate over begin when pNodes[0] is deleted
		TERARK_ASSERT_LT(idx, nNodes);
		TERARK_ASSERT_NE(pn[idx].link, delmark);
	#else
		// no extra guard is need for prev_i
		// if pn[0] was deleted, a valid caller should not make idx less than 0
		// because valid caller will not past beg_i() by calling prev_i
		do --idx;
		while (pn[idx].link == delmark);
	#endif
		return idx;
	}

	std::pair<size_t, bool>
	insert_i(const fstring key, const Value& val) {
		return lazy_insert_i(key, CopyConsFunc<Value>(val));
	}
	std::pair<size_t, bool>
	insert_i(const fstring key, Value&& val) {
		return lazy_insert_i(key, MoveConsFunc<Value>(std::move(val)));
	}
	std::pair<size_t, bool>
	insert_i(const fstring key) {
		return lazy_insert_i(key, &default_cons<Value>);
	}

	template<class ConsValue>
	std::pair<size_t, bool>
	lazy_insert_i(const fstring key, ConsValue cons_value) {
		TERARK_ASSERT_GE(key.n, 0);
		size_t n = nNodes; // compiler is easier to track local var
		HashTp h = hash(key);
		size_t i = size_t(h % nBucket);
		for (LinkTp p = bucket[i]; tail != p; p = pNodes[p].link) {
			TERARK_ASSERT_LT(p, n);
			const Node* y = &pNodes[p];
			TERARK_ASSERT_LT(y[0].offset, y[1].offset);
			TERARK_ASSERT_LE(y[1].offset, maxpool);
		// doesn't need to compare cached hash value, it almost always true
			size_t ybeg = LOAD_OFFSET(y[0].offset);
			size_t yend = LOAD_OFFSET(y[1].offset);
			size_t ylen = yend - ybeg - extralen(yend);
			if (equal(key, fstring(strpool + ybeg, ylen)))
				return std::make_pair(p, false);
		}
		if (terark_unlikely(n >= maxload)) {
			i = h % rehash(nBucket + 1);
		}
		TERARK_ASSERT_LE(n, maxNodes);
		size_t real_len = fstring_func::align_to(key.n+1);
        size_t slot = alloc_slot(real_len);
        size_t xbeg = LOAD_OFFSET(pNodes[slot+0].offset);
        size_t xend = LOAD_OFFSET(pNodes[slot+1].offset);
        TERARK_ASSERT_EQ(xend - xbeg, real_len);
        TERARK_ASSERT_LT(slot, nNodes);
		((align_type*)(strpool + xend))[-1] = 0; // pad 0
		strpool[xend-1] = (char)(real_len - key.n - 1); // extra-1
		memcpy(strpool + xbeg, key.p, key.n);
		cons_value(&nth_value(slot)); // must success
		pNodes[slot].link = bucket[i]; // newer at head
		bucket[i] = LinkTp(slot); // new head of i'th bucket
	  #if defined(HSM_ENABLE_HASH_CACHE)
		if (intptr_t(pHash) != hash_cache_disabled) {
			TERARK_ASSERT_NE(NULL, pHash);
			pHash[slot] = h;
		}
	  #endif
		sort_flag = en_unsorted;
		return std::make_pair(slot, true);
	}
	template<class ConsValue, class PreInsert>
	auto lazy_insert_i(fstring key, ConsValue cons_value, PreInsert pre_insert)
	-> typename // pre_insert is not void and it is convertable to bool
	std::enable_if<!std::is_same<decltype(pre_insert(this)), void>::value,
					std::pair<size_t, bool> >::type
	{
		TERARK_ASSERT_GE(key.n, 0);
		const HashTp h = hash(key);
		const size_t old_nBucket = nBucket;
		size_t i = size_t(h % old_nBucket);
		// this func is a copy-paste except old_nBucket and pre_insert
		for (LinkTp p = bucket[i]; tail != p; p = pNodes[p].link) {
			TERARK_ASSERT_LT(p, nNodes);
			const Node* y = &pNodes[p];
			TERARK_ASSERT_LT(y[0].offset, y[1].offset);
			TERARK_ASSERT_LE(y[1].offset, maxpool);
		// doesn't need to compare cached hash value, it almost always true
			size_t ybeg = LOAD_OFFSET(y[0].offset);
			size_t yend = LOAD_OFFSET(y[1].offset);
			size_t ylen = yend - ybeg - extralen(yend);
			if (equal(key, fstring(strpool + ybeg, ylen)))
				return std::make_pair(p, false);
		}
		if (!pre_insert(this)) {
			return std::make_pair(nNodes, true);
		}
		if (terark_unlikely(nNodes >= maxload)) {
			i = h % rehash(nBucket + 1);
		}
		else if (terark_unlikely(nBucket != old_nBucket)) {
			i = h % nBucket;
		}
		TERARK_ASSERT_LE(nNodes, maxNodes);
		size_t real_len = fstring_func::align_to(key.n+1);
		size_t slot = alloc_slot(real_len);
		size_t xbeg = LOAD_OFFSET(pNodes[slot+0].offset);
		size_t xend = LOAD_OFFSET(pNodes[slot+1].offset);
		TERARK_ASSERT_EQ(xend - xbeg, real_len);
		TERARK_ASSERT_LT(slot, nNodes);
		((align_type*)(strpool + xend))[-1] = 0; // pad 0
		strpool[xend-1] = (char)(real_len - key.n - 1); // extra-1
		memcpy(strpool + xbeg, key.p, key.n);
		cons_value(&nth_value(slot)); // must success
		pNodes[slot].link = bucket[i]; // newer at head
		bucket[i] = LinkTp(slot); // new head of i'th bucket
	  #if defined(HSM_ENABLE_HASH_CACHE)
		if (intptr_t(pHash) != hash_cache_disabled) {
			TERARK_ASSERT_NE(NULL, pHash);
			pHash[slot] = h;
		}
	  #endif
		sort_flag = en_unsorted;
		return std::make_pair(slot, true);
	}

	// same as lazy_insert_i overload, but pre_insert returns void
	template<class ConsValue, class PreInsert>
	auto lazy_insert_i(fstring key, ConsValue cons_value, PreInsert pre_insert)
	-> typename
	std::enable_if<std::is_same<decltype(pre_insert(this)), void>::value,
				   std::pair<size_t, bool> >::type
	{
		TERARK_ASSERT_GE(key.n, 0);
		const HashTp h = hash(key);
		const size_t old_nBucket = nBucket;
		size_t i = size_t(h % old_nBucket);
		// this func is a copy-paste except old_nBucket and pre_insert
		for (LinkTp p = bucket[i]; tail != p; p = pNodes[p].link) {
			TERARK_ASSERT_LT(p, nNodes);
			const Node* y = &pNodes[p];
			TERARK_ASSERT_LT(y[0].offset, y[1].offset);
			TERARK_ASSERT_LE(y[1].offset, maxpool);
		// doesn't need to compare cached hash value, it almost always true
			size_t ybeg = LOAD_OFFSET(y[0].offset);
			size_t yend = LOAD_OFFSET(y[1].offset);
			size_t ylen = yend - ybeg - extralen(yend);
			if (equal(key, fstring(strpool + ybeg, ylen)))
				return std::make_pair(p, false);
		}
		pre_insert(this); // returns void
		if (terark_unlikely(nNodes >= maxload)) {
			i = h % rehash(nBucket + 1);
		}
		else if (terark_unlikely(nBucket != old_nBucket)) {
			i = h % nBucket;
		}
		TERARK_ASSERT_LE(nNodes, maxNodes);
		size_t real_len = fstring_func::align_to(key.n+1);
		size_t slot = alloc_slot(real_len);
		size_t xbeg = LOAD_OFFSET(pNodes[slot+0].offset);
		size_t xend = LOAD_OFFSET(pNodes[slot+1].offset);
		TERARK_ASSERT_EQ(xend - xbeg, real_len);
		TERARK_ASSERT_LT(slot, nNodes);
		((align_type*)(strpool + xend))[-1] = 0; // pad 0
		strpool[xend-1] = (char)(real_len - key.n - 1); // extra-1
		memcpy(strpool + xbeg, key.p, key.n);
		cons_value(&nth_value(slot)); // must success
		pNodes[slot].link = bucket[i]; // newer at head
		bucket[i] = LinkTp(slot); // new head of i'th bucket
	  #if defined(HSM_ENABLE_HASH_CACHE)
		if (intptr_t(pHash) != hash_cache_disabled) {
			TERARK_ASSERT_NE(NULL, pHash);
			pHash[slot] = h;
		}
	  #endif
		sort_flag = en_unsorted;
		return std::make_pair(slot, true);
	}

	size_t find_i(const char* key, size_t len) const { return find_i(fstring(key, len)); }
	size_t find_i(const fstring key) const {
		TERARK_ASSERT_GE(key.n, 0);
		size_t n = nNodes; // compiler is easier to track local var
		HashTp h = hash(key);
		size_t i = size_t(h % nBucket);
		for (LinkTp p = bucket[i]; tail != p; p = pNodes[p].link) {
			TERARK_ASSERT_LT(p, n);
			const Node* y = &pNodes[p];
			TERARK_ASSERT_LT(y[0].offset, y[1].offset);
			TERARK_ASSERT_LE(y[1].offset, maxpool);
		// doesn't need to compare cached hash value, it almost always true
			size_t ybeg = LOAD_OFFSET(y[0].offset);
			size_t yend = LOAD_OFFSET(y[1].offset);
			size_t ylen = yend - ybeg - extralen(yend);
		   	if (equal(key, fstring(strpool + ybeg, ylen)))
				return p;
		}
		return n; // not found
	}

	size_t count(const fstring key) const {
		TERARK_ASSERT_GE(key.n, 0);
		return find_i(key) == nNodes ? 0 : 1;
	}
	// return value is same with count(key), but type is different
	// this function is not stl standard, but more meaningful for app
	bool exists(const fstring key) const {
		TERARK_ASSERT_GE(key.n, 0);
		return find_i(key) != nNodes;
	}
	bool contains(const fstring key) const { // synonym of exists
		TERARK_ASSERT_GE(key.n, 0);
		return find_i(key) != nNodes;
	}

private:
    void put_to_freelist(LinkTp slot) {
		size_t mybeg = pNodes[slot+0].offset;
		size_t myend = pNodes[slot+1].offset;
        ptrdiff_t fastIdx = myend - mybeg - 1;
		mybeg = LOAD_OFFSET(mybeg);
		myend = LOAD_OFFSET(myend);
        FreeLink& fl = (FreeLink&)(strpool[mybeg]);
        FreeList& li = fastlist[std::min(fastIdx, ptrdiff_t(fastleng))];
        fl.next = li.head;
        li.head = slot;
      #if HSM_FreeListWithDebug
        li.freq++;
        li.llen++;
      #endif
    }

    LinkTp alloc_slot(size_t real_len) {
		TERARK_ASSERT_AL(real_len, SP_ALIGN);
		if constexpr (WithFreeList) {
			if (freelist_disabled != fastleng) {
				ptrdiff_t fastIdx = SAVE_OFFSET(real_len - 1);
				if (fastIdx < fastleng) {
					LinkTp slot = fastlist[fastIdx].head;
					if (terark_likely(tail != slot))
						return alloc_in_fastlist(real_len, fastIdx, slot);
				} else {
					LinkTp slot = alloc_in_hugelist(real_len);
					if (tail != slot)
						return slot;
				}
			}
		} // if constexpr (WithFreeList)
        // make new slot
		if (terark_unlikely(nNodes == maxNodes)) {
			TERARK_VERIFY_LT(nNodes, maxlink);
			reserve_nodes(0 == nNodes ? 1 : (nNodes + 1) * 103 / 64);
		}
		if (terark_unlikely(SAVE_OFFSET(real_len) > maxpool - lenpool)) {
			gc_on_strpool_is_full(real_len);
		}
        lenpool += SAVE_OFFSET(real_len);
		pNodes[nNodes+1].offset = lenpool;
		pNodes[nNodes+1].link = tail; // guard for next_i
        return nNodes++;
    }

	terark_forceinline
	LinkTp alloc_in_fastlist(size_t real_len, ptrdiff_t fastIdx, LinkTp slot) {
		size_t mybeg = LOAD_OFFSET(pNodes[slot+0].offset);
	#if defined(_DEBUG) || !defined(NDEBUG)
		size_t myend = LOAD_OFFSET(pNodes[slot+1].offset);
		TERARK_ASSERT_EQ(myend - mybeg, real_len);
	#endif
	#if HSM_FreeListWithDebug
		fastlist[fastIdx].llen--;
	#endif
		fastlist[fastIdx].head = ((FreeLink&)strpool[mybeg]).next;
		TERARK_ASSERT_GE(LOAD_OFFSET(freepool), real_len);
		TERARK_ASSERT_GE(nDeleted, 1);
		freepool -= SAVE_OFFSET(real_len);
		nDeleted--;
		return slot;
	}

	LinkTp alloc_in_hugelist(size_t real_len) {
		FreeList& hugelist = fastlist[fastleng];
		LinkTp* curp = &hugelist.head;
		LinkTp  curr;
		size_t search_max = 5; // to avoid perf decay
		size_t search_num = 0;
		while ((curr = *curp) != tail && search_num++ < search_max) {
			size_t mybeg = LOAD_OFFSET(pNodes[curr+0].offset);
			size_t myend = LOAD_OFFSET(pNodes[curr+1].offset);
			LinkTp* next = &((FreeLink&)strpool[mybeg]).next;
			if (myend - mybeg == real_len) {
				*curp = *next;
			#if HSM_FreeListWithDebug
				hugelist.llen--;
			#endif
				TERARK_ASSERT_GE(LOAD_OFFSET(freepool), real_len);
				TERARK_ASSERT_GE(nDeleted, 1);
				freepool -= SAVE_OFFSET(real_len);
				nDeleted--;
				return curr;
			}
			curp = next;
		}
		return tail;
	}

	void gc_on_strpool_is_full(size_t real_len) {
		using namespace std; // for std::max
		TERARK_VERIFY_LE(LOAD_OFFSET(lenpool - freepool) + real_len, maxoffset);
		if (freelist_disabled == fastleng && LOAD_OFFSET(freepool) >= max(LOAD_OFFSET(maxpool)/2, real_len))
			revoke_deleted();
		else { // if lenpool is random, expansion scale is about 1.6
			size_t expect = pow2_align_up((LOAD_OFFSET(lenpool) + real_len) * 103 / 64, 64);
			size_t newmax = min(expect, (size_t)maxoffset);
			char*  newpch = (char*)realloc(strpool, newmax);
			if (NULL == newpch) {
				if (freelist_disabled == fastleng && LOAD_OFFSET(freepool) >= real_len)
					revoke_deleted();
				else
					TERARK_DIE("realloc(%zd)", newmax);
			} else {
				maxpool = SAVE_OFFSET(newmax);
				strpool = newpch;
			}
		}
	}

	void ulink_impl(const LinkTp idx, const HashTp h) {
		size_t const i = size_t(h % nBucket);
		TERARK_ASSERT_NE(tail, bucket[i]);
		LinkTp* p = &bucket[i];
		while (idx != *p) {
			p = &pNodes[*p].link;
			TERARK_ASSERT_LT(*p, nNodes);
		}
		*p = pNodes[idx].link; // delete the node from link list
	}

public:
	/// precond: # Elem on slot is constructed
	///          # Elem on slot is not in the table:
	///                 it is not reached through 'bucket'
	/// effect: destory the Elem on slot then free the slot
	void risk_slot_free(size_t idx) {
		TERARK_ASSERT_GE(nNodes, 1);
		TERARK_ASSERT_LT(idx, nNodes);
		TERARK_ASSERT_NE(pNodes[idx].link, delmark); // must has not deleted
	  #if defined(HSM_ENABLE_HASH_CACHE)
		TERARK_ASSERT_F(intptr_t(pHash) == hash_cache_disabled || NULL != pHash, "%p", pHash);
	  #endif
		pNodes[idx].link = delmark; // set deletion mark
		nth_value(idx).~Value(); // destroy
		size_t mybeg = LOAD_OFFSET(pNodes[idx+0].offset);
		size_t myend = LOAD_OFFSET(pNodes[idx+1].offset);
		size_t mylen = myend - mybeg;
		if (terark_unlikely(nNodes-1 == idx)) {
			nNodes--;
			pNodes[idx].link = tail; // guard for next_i
			TERARK_ASSERT_GE(LOAD_OFFSET(lenpool), mylen);
			lenpool = SAVE_OFFSET(mybeg);
			return;
		}
		freepool += SAVE_OFFSET(mylen);
		++nDeleted;
	  if constexpr (WithFreeList) {
        if (freelist_disabled == fastleng) {
            if (terark_unlikely(nDeleted >= nNodes/2))
    			revoke_deleted();
    	//	relaxed: allow for key sorted and POD value sorted
        //	if (!boost::has_trivial_destructor<Value>::value && en_sort_by_val == sort_flag)
        //    	sort_flag = en_unsorted;
        } else
            put_to_freelist(LinkTp(idx));
	  } else {
		if (terark_unlikely(nDeleted >= nNodes/2))
			revoke_deleted();
	  }
	}

	// unlink from bucket and collision list, but keep the elem valid,
	// and delmark is not set, users should call risk_slot_free(slot) later.
	void risk_unlink(const size_t slot) {
		TERARK_ASSERT_GE(nNodes, 1);
		TERARK_ASSERT_LT(slot, nNodes);
		TERARK_ASSERT_NE(pNodes[slot].link, delmark); // must has not deleted
	  #if defined(HSM_ENABLE_HASH_CACHE)
		TERARK_ASSERT_F(intptr_t(pHash) == hash_cache_disabled || NULL != pHash, "%p", pHash);
		HashTp h = intptr_t(pHash) == hash_cache_disabled ? hash(key(slot)) : pHash[slot];
	  #else
		HashTp h = hash(key(slot));
	  #endif
		ulink_impl(LinkTp(slot), h);
	}

public:
	void erase_i(const size_t idx) {
		TERARK_ASSERT_GE(nNodes, 1);
		TERARK_ASSERT_LT(idx, nNodes);
		TERARK_ASSERT_NE(pNodes[idx].link, delmark); // must has not deleted
		risk_unlink(idx);
		risk_slot_free(idx);
	}

    void enable_freelist(ptrdiff_t maxKeyLen = 120) {
		static_assert(WithFreeList);
		TERARK_VERIFY_GT(maxKeyLen, 0);
        TERARK_VERIFY_LT(maxKeyLen, 32*1024);
        ptrdiff_t newlistLen = SAVE_OFFSET(maxKeyLen + SP_ALIGN - 1);
		if (newlistLen == fastleng) {
			return;
		}
        FreeList* newlist = fastlist;
        const Node* pn = pNodes;
        char* ps = strpool;
        if (newlistLen < fastleng) {
            // move separated free slots to hugelist
			FreeList& hugelist = fastlist[fastleng];
            for (ptrdiff_t  i =  newlistLen; i < fastleng; ++i) {
                LinkTp* pcurr = &newlist[i].head;
                LinkTp  ihead =  newlist[i].head;
                if (tail != ihead) {
                    TERARK_ASSERT_GT(newlist[i].llen, 0);
                    do {
                        size_t mybeg = LOAD_OFFSET(pn[*pcurr].offset);
                        pcurr = &((FreeLink&)ps[mybeg]).next;
                    } while (tail != *pcurr);
                    *pcurr = hugelist.head;
                    hugelist.head  = ihead;
                  #if HSM_FreeListWithDebug
                    hugelist.freq += newlist[i].freq;
                    hugelist.llen += newlist[i].llen;
                  #endif
                }
            }
			fastlist[newlistLen] = fastlist[fastleng]; // hugelist
            fastleng = newlistLen;
        }
        newlist = t_realloc(newlist, newlistLen + 1);
        if (NULL == newlist) TERARK_DIE("realloc(%zd)", sizeof(FreeList)*(newlistLen+1));
		fastlist = newlist;
        if (freelist_disabled == fastleng) {
            fastleng = newlistLen;
            std::fill_n(newlist, newlistLen+1, FreeList());
            if (nDeleted) {
                for (LinkTp i = 0; i < nNodes; ++i) {
                    if (delmark == pn[i].link)
                        put_to_freelist(i);
                }
            }
        }
        else if (fastleng < newlistLen) {
			// enlarge fastlist & move suitable slots in hugelist to fastlist
			FreeList& hugelist = newlist[newlistLen] = newlist[fastleng];
            std::fill(newlist + fastleng, newlist + newlistLen, FreeList());
            // move slots from hugelist to fastlist
            LinkTp *pprev = &hugelist.head, curr = hugelist.head;
            while (tail != curr) {
                ptrdiff_t iFast = pn[curr+1].offset - pn[curr].offset - 1;
                FreeLink& rCurr = (FreeLink&)ps[LOAD_OFFSET(pn[curr].offset)];
                LinkTp next = rCurr.next;
                if (iFast < newlistLen) {
                    // put curr slot to iFast
                    FreeList& rFast = newlist[iFast];
                    rCurr.next = rFast.head;
                    rFast.head = curr;
                  #if HSM_FreeListWithDebug
                    rFast.freq++;
                    rFast.llen++;
                    hugelist.freq--;
                    hugelist.llen--;
                  #endif
                    *pprev = next; // delete curr from hugelist
                } else
                    pprev = &rCurr.next; // keep track, only non-deleted
                curr = next;
            }
        }
        fastleng = newlistLen;
    }

	const FreeList& get_fastlist(ptrdiff_t i) const {
		static_assert(WithFreeList);
		TERARK_ASSERT_LE(i, fastleng);
		return fastlist[i];
	}
	size_t get_fastleng() const { return fastleng; }

    void disable_freelist() {
		static_assert(WithFreeList);
		TERARK_VERIFY_F((NULL != fastlist && freelist_disabled != fastleng) ||
					    (NULL == fastlist && freelist_disabled == fastleng),
					    "fastlist = %p, fastleng = %d", fastlist, fastleng);
		if (fastlist) {
			free(fastlist);
			fastlist = NULL;
		}
        fastleng = freelist_disabled;
    }

	bool is_deleted(size_t idx) const {
		TERARK_ASSERT_GE(nNodes, 1);
		TERARK_ASSERT_LT(idx, nNodes);
		return pNodes[idx].link == delmark;
	}

	const char* key_c_str(size_t idx) const {
		TERARK_ASSERT_GE(nNodes, 1);
		TERARK_ASSERT_LT(idx, nNodes);
		size_t mybeg = LOAD_OFFSET(pNodes[idx+0].offset);
		TERARK_ASSERT_LT(mybeg, LOAD_OFFSET(lenpool));
		return strpool + mybeg;
	}
	size_t key_len(size_t idx) const {
		TERARK_ASSERT_GE(nNodes, 1);
		TERARK_ASSERT_LT(idx, nNodes);
		size_t mybeg = LOAD_OFFSET(pNodes[idx+0].offset);
		size_t myend = LOAD_OFFSET(pNodes[idx+1].offset);
		size_t extra = extralen(myend);
		size_t mylen = myend - mybeg - extra;
		TERARK_ASSERT_LT(mybeg, myend);
		TERARK_ASSERT_LE(myend, LOAD_OFFSET(lenpool));
		return mylen;
	}
	size_t key_offset(size_t idx) const {
		TERARK_ASSERT_GE(nNodes, 1);
		TERARK_ASSERT_LT(idx, nNodes);
		size_t mybeg = LOAD_OFFSET(pNodes[idx+0].offset);
		TERARK_ASSERT_LT(mybeg, LOAD_OFFSET(lenpool));
		return mybeg;
	}
	size_t key_offset_raw(size_t idx) const {
	#if !defined(NDEBUG)
		TERARK_ASSERT_GE(nNodes, 1);
		TERARK_ASSERT_LT(idx, nNodes);
		size_t mybeg = LOAD_OFFSET(pNodes[idx+0].offset);
		TERARK_ASSERT_LT(mybeg, LOAD_OFFSET(lenpool));
	#endif
		return pNodes[idx+0].offset;
	}
	fstring key(size_t idx) const {
		TERARK_ASSERT_GE(nNodes, 1);
		TERARK_ASSERT_LT(idx, nNodes);
		size_t mybeg = LOAD_OFFSET(pNodes[idx+0].offset);
		size_t myend = LOAD_OFFSET(pNodes[idx+1].offset);
		size_t extra = extralen(myend);
		size_t mylen = myend - mybeg - extra;
		TERARK_ASSERT_LT(mybeg, myend);
		TERARK_ASSERT_LE(myend, LOAD_OFFSET(lenpool));
		return fstring(strpool + mybeg, mylen);
	}

	fstring end_key(size_t idxEnd) const {
		TERARK_ASSERT_GE(nNodes, 1);
		TERARK_ASSERT_LE(idxEnd, nNodes);
		size_t idx = nNodes - idxEnd;
		size_t mybeg = LOAD_OFFSET(pNodes[idx+0].offset);
		size_t myend = LOAD_OFFSET(pNodes[idx+1].offset);
		size_t extra = extralen(myend);
		size_t mylen = myend - mybeg - extra;
		TERARK_ASSERT_LT(mybeg, myend);
		TERARK_ASSERT_LE(myend, LOAD_OFFSET(lenpool));
		return fstring(strpool + mybeg, mylen);
	}

private:
	template<class KeyValue, class Func>
	void for_each_impl(Func f) const {
		Node* pn = pNodes;
		char* ps = strpool;
		if (nDeleted) {
			for (size_t i = 0, n = nNodes; i < n; ++i) {
				// need check deletion mark
				if (pn[i].link != delmark) {
					const size_t mybeg = LOAD_OFFSET(pn[i+0].offset);
					const size_t myend = LOAD_OFFSET(pn[i+1].offset);
					const size_t extra = extralen(ps, myend);
					const size_t mylen = myend - mybeg - extra;
					TERARK_ASSERT_LT(mybeg, myend);
					TERARK_ASSERT_LE(myend, LOAD_OFFSET(lenpool));
					KeyValue kv(fstring(ps + mybeg, mylen)
							, nth_value(i, ValuePlace()));
					f(kv);
				}
			}
		}
		else { // need not to check deletion mark
			for (size_t i = 0, n = nNodes; i < n; ++i) {
				const size_t mybeg = LOAD_OFFSET(pn[i+0].offset);
				const size_t myend = LOAD_OFFSET(pn[i+1].offset);
				const size_t extra = extralen(ps, myend);
				const size_t mylen = myend - mybeg - extra;
				TERARK_ASSERT_LT(mybeg, myend);
				TERARK_ASSERT_LE(myend, LOAD_OFFSET(lenpool));
				KeyValue kv(fstring(ps + mybeg, mylen)
						, nth_value(i, ValuePlace()));
				f(kv);
			}
		}
	}
public:
	///@param f Func(kv)
	template<class Func>
	void for_each(Func f) const {
		for_each_impl<typename const_iterator::value_type, Func>(f);
	}
	template<class Func>
	void for_each(Func f) {
		for_each_impl<typename iterator::value_type, Func>(f);
	}
	template<class Func>
	void for_each_key(Func f) const {
		Node* pn = pNodes;
		char* ps = strpool;
		if (nDeleted) {
			for (size_t i = 0, n = nNodes; i < n; ++i) {
				// need check deletion mark
				if (pn[i].link != delmark) {
					const size_t mybeg = LOAD_OFFSET(pn[i+0].offset);
					const size_t myend = LOAD_OFFSET(pn[i+1].offset);
					const size_t extra = extralen(ps, myend);
					const size_t mylen = myend - mybeg - extra;
					TERARK_ASSERT_LT(mybeg, myend);
					TERARK_ASSERT_LE(myend, LOAD_OFFSET(lenpool));
					f(fstring(ps + mybeg, mylen));
				}
			}
		}
		else { // need not to check deletion mark
			for (size_t i = 0, n = nNodes; i < n; ++i) {
				const size_t mybeg = LOAD_OFFSET(pn[i+0].offset);
				const size_t myend = LOAD_OFFSET(pn[i+1].offset);
				const size_t extra = extralen(ps, myend);
				const size_t mylen = myend - mybeg - extra;
				TERARK_ASSERT_LT(mybeg, myend);
				TERARK_ASSERT_LE(myend, LOAD_OFFSET(lenpool));
				f(fstring(ps + mybeg, mylen));
			}
		}
	}
	///@param f Func(const& val)
	template<class Func>
	void for_each_val(Func f) const {
		if (nDeleted) {
			Node* pn = pNodes;
			for (size_t i = 0, n = nNodes; i < n; ++i)
				if (pn[i].link != delmark)
					f((const Value&)nth_value(i, ValuePlace()));
		}
		else { // need not to check deletion mark
			for (size_t i = 0, n = nNodes; i < n; ++i)
				f((const Value&)nth_value(i, ValuePlace()));
		}
	}
	///@param f Func(non-const& val)
	template<class Func>
	void for_each_val(Func f) {
		if (nDeleted) {
			Node* pn = pNodes;
			for (size_t i = 0, n = nNodes; i < n; ++i)
				if (pn[i].link != delmark)
					f(nth_value(i, ValuePlace()));
		}
		else { // need not to check deletion mark
			for (size_t i = 0, n = nNodes; i < n; ++i)
				f(nth_value(i, ValuePlace()));
		}
	}

// Sorting and Binary search support:
private:
	struct KeyIndex {
		// use this struct instead of just a 'int' index to pNodes could
		// saved 1 indirect memory access to pNodes[*], friendly to CPU cache
		LinkTp offset;
		LinkTp length;
		LinkTp idx; // index to pNodes
	};
	template<class KeyCompare>
	struct SortKeyCompare : KeyCompare {
		const char* ps;
		bool operator()(const KeyIndex& x, const KeyIndex& y) const {
			fstring sx(ps + LOAD_OFFSET(x.offset), x.length);
			fstring sy(ps + LOAD_OFFSET(y.offset), y.length);
			return KeyCompare::operator()(sx, sy);
		}
		bool operator()(const Node& x, const Node& y) const {
			fstring sx(ps + LOAD_OFFSET(x.offset), x.link); // link is length
			fstring sy(ps + LOAD_OFFSET(y.offset), y.link);
			return KeyCompare::operator()(sx, sy);
		}
		SortKeyCompare(KeyCompare cmp, const char* ps) : KeyCompare(cmp), ps(ps) {}
	};

	void save_strlen_to_link() {
		Node* pn = pNodes;
		char* ps = strpool;
		for (size_t i = 0, n = nNodes; i < n; ++i) {
			const size_t mybeg = LOAD_OFFSET(pn[i+0].offset);
			const size_t myend = LOAD_OFFSET(pn[i+1].offset);
			const size_t extra = extralen(ps, myend);
			const size_t mylen = myend - mybeg - extra;
			TERARK_ASSERT_LE(extra, SP_ALIGN);
			TERARK_ASSERT_LT(mybeg, myend);
			TERARK_ASSERT_LE(myend, LOAD_OFFSET(lenpool));
			pn[i].link = LinkTp(mylen); // 'link' save the strlen
		}
	}

	KeyIndex* buildindex() {
		KeyIndex* index = (KeyIndex*)malloc(sizeof(KeyIndex) * nNodes);
		if (NULL == index) {
			TERARK_DIE("malloc(%zd)", sizeof(KeyIndex) * nNodes);
		}
		Node* pn = pNodes;
		char* ps = strpool;
		for (size_t i = 0, n = nNodes; i < n; ++i) {
			const size_t mybeg = LOAD_OFFSET(pn[i+0].offset);
			const size_t myend = LOAD_OFFSET(pn[i+1].offset);
			const size_t extra = extralen(ps, myend);
			const size_t mylen = myend - mybeg - extra;
			TERARK_ASSERT_LE(extra, SP_ALIGN);
			TERARK_ASSERT_LT(mybeg, myend);
			TERARK_ASSERT_LE(myend, LOAD_OFFSET(lenpool));
			index[i].offset = SAVE_OFFSET(mybeg);
			index[i].length = pn[i].link = LinkTp(mylen);
			index[i].idx = LinkTp(i);
		}
		return index;
	}

	LinkTp* buildindex_by_int() {
		LinkTp* p = (LinkTp*)malloc(sizeof(LinkTp) * nNodes);
		if (NULL == p) {
			TERARK_DIE("malloc(%zd)", sizeof(LinkTp) * nNodes);
		}
		for (size_t i = 0, n = nNodes; i < n; ++i)
			p[i] = i;
		return p;
	}

 // template Arg  HasHashCache this will enable compile time optimization
 	template<bool HasHashCache>
	void rearrange_nodes_by_int_impl(LinkTp* index) {
		Node* pn = pNodes;
		Value* pv = values;
	  #if defined(HSM_ENABLE_HASH_CACHE)
		HashTp*ph = pHash;
	  #endif
		for (size_t i = 0, n = nNodes; i < n; ++i) { // Rearrange
			if (index[i] != i) {
				LinkTp curr, next = index[i];
				LinkTp tmpoffset = pn[next].offset;
				LinkTp tmplink   = pn[next].link; // link saved strlen
			  #if defined(HSM_ENABLE_HASH_CACHE)
				HashTp tmphash   = 0; // avoid warning
			  #endif
				char   tmpvalue[sizeof(Value)];
			  #if defined(HSM_ENABLE_HASH_CACHE)
                if (HasHashCache)
                    tmphash = ph[next];
			  #endif
				CopyStrategy::move_cons((Value*)tmpvalue, nth_value(next));
				do {
					TERARK_ASSERT_LT(next, n);
					curr = next;
					next = index[next];
					pn[curr].offset = pn[next].offset;
					pn[curr].link   = pn[next].link;
					move_value_on_raw(pn+curr, pn+next, pv+curr, pv+next);
					index[curr] = curr;
				  #if defined(HSM_ENABLE_HASH_CACHE)
					if (HasHashCache)
						ph[curr] = ph[next];
				  #endif
				} while (next != i);
#if defined(__GNUC__) && __GNUC__*1000 + __GNUC_MINOR__ >= 4006
		#pragma GCC diagnostic push
		#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#endif
				CopyStrategy::move_cons(&nth_value(i), *reinterpret_cast<Value*>(tmpvalue));
#if defined(__GNUC__) && __GNUC__*1000 + __GNUC_MINOR__ >= 4006
		#pragma GCC diagnostic pop
#endif
				pn[i].offset = tmpoffset;
				pn[i].link   = tmplink;
			  #if defined(HSM_ENABLE_HASH_CACHE)
                if (HasHashCache)
					ph[i] = tmphash;
			  #endif
				index[i] = LinkTp(i);
			}
		}
		free(index);
	}
	void rearrange_nodes_by_int(LinkTp* index) {
	  #if defined(HSM_ENABLE_HASH_CACHE)
		if (intptr_t(pHash) == hash_cache_disabled || NULL == pHash)
			rearrange_nodes_by_int_impl<false>(index); // "index" will be free'ed
		else
			rearrange_nodes_by_int_impl<true >(index); // "index" will be free'ed
	  #else
		rearrange_nodes_by_int_impl<false>(index); // "index" will be free'ed
	  #endif
	}

	template<class IndexType>
	LinkTp* copy_idx_to_LinkTp_array(IndexType* index) {
		// copy idx to LinkTp array, small array has benefit of CPU cache
		// in rearrange_nodes_by_int
		LinkTp* pi = reinterpret_cast<LinkTp*>(index);
		for (size_t i = 0, n = nNodes; i < n; ++i)
			pi[i] = index[i].idx;
		return pi;
	}

	template<class IndexType>
	void rearrange_nodes(IndexType* index) {
		LinkTp* pi = copy_idx_to_LinkTp_array(index);
		rearrange_nodes_by_int(pi); // pi == index will be free'ed
	}

	void rearrange_strpool() {
		char* s2 = (char*)malloc(LOAD_OFFSET(lenpool));
		if (NULL == s2) {
			TERARK_DIE("malloc(%zd)", LOAD_OFFSET(lenpool));
		}
		size_t loffset = 0;
		char* ps = strpool;
		Node* pn = pNodes;
		for (size_t i = 0, n = nNodes; i < n; ++i) {
			size_t len = pn[i].link; // Node::link saved string length
			len = fstring_func::align_to(len + 1);
			// could be optimized as aligned copy?
			memcpy(s2 + loffset, ps + LOAD_OFFSET(pn[i].offset), len);
			pn[i].offset = SAVE_OFFSET(loffset);
			loffset += len;
		}
		TERARK_VERIFY_EQ(loffset, LOAD_OFFSET(lenpool));
		free(strpool);
		strpool = s2;
		maxpool = lenpool;
	}

	template<class KeyCompare>
	void sort_by_key_impl(const KeyCompare& comp, sort_by_index_yes) {
		KeyIndex* index = buildindex();
		terark_parallel_sort(index, index + nNodes, SortKeyCompare<KeyCompare>(comp, strpool));
		rearrange_nodes(index);
	}
	template<class KeyCompare>
	void sort_by_key_impl(const KeyCompare& comp, sort_by_index_no) {
		save_strlen_to_link();
		terark_parallel_sort(pNodes, pNodes + nNodes, SortKeyCompare<KeyCompare>(comp, strpool));
	}

	typedef typename bytes2uint<(sizeof(LinkTp)*2<SP_ALIGN)?sizeof(LinkTp)*2:SP_ALIGN>::type LinkT2;
	typedef KeyIndexWithPrefix_tt<LinkTp>            KeyIndexWithPrefix;
	typedef     NodeWithPrefix_tt<LinkTp, LinkT2, Value> NodeWithPrefix;

	KeyIndexWithPrefix*	sort_by_key_prefix_o(sort_by_index_yes, bool DoRearrange) {
		KeyIndexWithPrefix* index = t_malloc<KeyIndexWithPrefix>(nNodes);
		if (NULL == index) {
			TERARK_DIE("malloc(%zd)", sizeof(KeyIndexWithPrefix) * nNodes);
		}
		Node* pn = pNodes;
		char* ps = strpool;
		for (size_t i = 0, n = nNodes; i < n; ++i) {
			const size_t mybeg = LOAD_OFFSET(pn[i+0].offset);
			const size_t myend = LOAD_OFFSET(pn[i+1].offset);
			const size_t extra = extralen(ps, myend);
			const size_t mylen = myend - mybeg - extra;
			TERARK_ASSERT_LE(extra, SP_ALIGN);
			TERARK_ASSERT_LT(mybeg, myend);
			TERARK_ASSERT_LE(myend, LOAD_OFFSET(lenpool));
			index[i].offset = SAVE_OFFSET(mybeg);
			index[i].length = pn[i].link = LinkTp(mylen);
			if (mylen >= sizeof(index[i].prefix))
				memcpy(&index[i].prefix, ps + mybeg, sizeof(index[i].prefix));
			else {
				index[i].prefix = 0;
				memcpy(&index[i].prefix, ps + mybeg, mylen);
			}
#if defined(BOOST_ENDIAN_LITTLE_BYTE)
			index[i].prefix = terark::byte_swap(index[i].prefix);
#endif
			index[i].idx = LinkTp(i);
		}
		terark_parallel_sort(index, index + nNodes, ByteWiseKeyCompare(strpool));
		if (DoRearrange) {
			rearrange_nodes(index); // "index" will be free'ed
			return NULL;
		}
		return index;
	}
	void sort_by_key_prefix_o(sort_by_index_no, bool DoRearrange) {
		TERARK_VERIFY_EQ(DoRearrange, true);
		NodeWithPrefix* pnp;
		Node* pn;
		if (boost::is_same<CopyStrategy, FastCopy>::value) {
			pnp = (NodeWithPrefix*)realloc(pNodes, sizeof(NodeWithPrefix)*nNodes);
			pn = (Node*)pnp;
		} else {
			pnp = (NodeWithPrefix*)malloc(sizeof(NodeWithPrefix)*nNodes);
			pn = pNodes;
		}
		if (NULL == pnp) { // fallback to slower version
			sort_by_key_impl(fstring_func::IF_SP_ALIGN(less_align, less_unalign)(), sort_by_index_no());
			return;
		}
		char* ps = strpool;
		// backward copy to pnp because it is inplace copy small to big
		for (ptrdiff_t i = nNodes-1; i >= 0; --i) {
			const size_t mybeg = LOAD_OFFSET(pn[i+0].offset);
			const size_t myend = LOAD_OFFSET(pn[i+1].offset);
			const size_t extra = extralen(ps, myend);
			const size_t mylen = myend - mybeg - extra;
			TERARK_ASSERT_LE(extra, SP_ALIGN);
			TERARK_ASSERT_LT(mybeg, myend);
			TERARK_ASSERT_LE(myend, LOAD_OFFSET(lenpool));
			// TODO: &pnp[0].value may overlap with &pn[0].value, now just support FastCopy
			CopyStrategy::move_cons_backward(&pnp[i].value, pn[i].value);
			if (mylen >= sizeof(pnp[i].prefix))
				memcpy(&pnp[i].prefix, ps + mybeg, sizeof(pnp[i].prefix));
			else {
				pnp[i].prefix = 0;
				memcpy(&pnp[i].prefix, ps + mybeg, mylen);
			}
#if defined(BOOST_ENDIAN_LITTLE_BYTE)
			pnp[i].prefix = terark::byte_swap(pnp[i].prefix);
#endif
			pnp[i].length = LinkTp(mylen);
			pnp[i].offset = SAVE_OFFSET(mybeg);
		}
		terark_parallel_sort(pnp, pnp + nNodes, ByteWiseKeyCompare(strpool));
		for (size_t i = 0; i < nNodes; ++i) {
			pn[i].offset = pnp[i].offset;
			pn[i].link = pnp[i].length;
			CopyStrategy::move_cons_forward(&pn[i].value, pnp[i].value);
		}
		pn[nNodes].offset = lenpool;
		pn[nNodes].link = tail;
		if (boost::is_same<CopyStrategy, FastCopy>::value) {
			pNodes = pn;
			pn = (Node*)realloc(pn, allnodes_size(maxNodes));
			if (NULL == pn) TERARK_DIE("realloc(%zd)", allnodes_size(maxNodes));
			pNodes = pn;
		} else
			free(pnp); // malloc'ed
	}

public:
	template<class KeyCompare>
	void sort(const KeyCompare& comp) {
		short old_fastleng = fastleng;
		if (nDeleted) {
			if constexpr (WithFreeList) {
				if (freelist_disabled != old_fastleng)
					disable_freelist();
			}
			revoke_deleted_no_relink();
		}
		if (nNodes >= 2) {
			sort_by_key_impl(comp, sort_by_index());
			rearrange_strpool(); // must after rearrange_nodes
			if (bucket) // is hash disabled?
				relink_fill();
		}
		if constexpr (WithFreeList) {
			if (freelist_disabled != old_fastleng)
				enable_freelist(old_fastleng);
		}
		sort_flag = en_sort_by_key;
	}
	void sort_slow() { sort(fstring_func::IF_SP_ALIGN(less_align, less_unalign)()); return; }
	void sort_fast() { // with_prefix_optimization
		short old_fastleng = fastleng;
		if (nDeleted) {
			if constexpr (WithFreeList) {
				if (freelist_disabled != old_fastleng)
					disable_freelist();
			}
			revoke_deleted_no_relink();
		}
		if (nNodes >= 2) {
			const bool DoRearrange = true;
			sort_by_key_prefix_o(sort_by_index(), DoRearrange);
			rearrange_strpool(); // must after rearrange_nodes
			if (bucket) // is hash disabled?
				relink_fill();
		}
		if constexpr (WithFreeList) {
			if (freelist_disabled != old_fastleng)
				enable_freelist(old_fastleng);
		}
		sort_flag = en_sort_by_key;
   	}

	///@note returned pointer should be freed by the caller
	LinkTp* get_sorted_index_fast() const {
		TERARK_VERIFY_EZ(nDeleted);
		KeyIndexWithPrefix* idx0;
	   	LinkTp* idx1;
		const bool DoRearrange = false;
		idx0 = sort_by_key_prefix_o(sort_by_index_yes(), DoRearrange);
		idx1 = copy_idx_to_LinkTp_array(idx0);
		idx1 = (LinkTp*)realloc(idx1, sizeof(LinkTp) * nNodes); // shrink
		TERARK_VERIFY(NULL != idx1);
		return idx1;
	}

	///@note length of *buf must >= this->end_i()+1, or NULL
	void get_offsets(LinkTp** ppbuf) const {
		if (NULL == *ppbuf)
			*ppbuf = (LinkTp)malloc(sizeof(LinkTp)*(nNodes+1));
		const Node* pn = pNodes;
		LinkTp* buf = *ppbuf;
		for (size_t i = 0, n = nNodes+1; i < n; ++i)
			buf[i] = pn[i].offset;
	}

	size_t lower_bound(const fstring key) const {
		return lower_bound(key, fstring_func::IF_SP_ALIGN(Less, less_unalign)());
	}
	size_t upper_bound(const fstring key) const {
		return upper_bound(key, fstring_func::IF_SP_ALIGN(Less, less_unalign)());
	}

	template<class KeyCompare>
	size_t lower_bound(const fstring key, const KeyCompare& comp) const {
		TERARK_VERIFY_EQ(en_sort_by_key, sort_flag);
		const Node* pn = &pNodes[0];
		const char* ps = strpool;
		size_t lo = 0, hi = nNodes;
		while (lo < hi) {
			size_t mid = lo + (hi - lo) / 2; // avoid overflow
			size_t beg = LOAD_OFFSET(pn[mid+0].offset);
			size_t End = LOAD_OFFSET(pn[mid+1].offset);
			size_t len = End - beg - extralen(ps, End);
			const fstring smid(ps + beg, len);
			if (comp(smid, key))
				lo = mid + 1;
			else
				hi = mid;
		}
		return lo;
	}

	template<class KeyCompare>
	size_t upper_bound(const fstring key, const KeyCompare& comp) const {
		TERARK_VERIFY_EQ(en_sort_by_key, sort_flag);
		const Node* pn = &pNodes[0];
		const char* ps = strpool;
		size_t lo = 0, hi = nNodes;
		while (lo < hi) {
			size_t mid = lo + (hi - lo) / 2; // avoid overflow
			size_t beg = LOAD_OFFSET(pn[mid+0].offset);
			size_t End = LOAD_OFFSET(pn[mid+1].offset);
			size_t len = End - beg - extralen(ps, End);
			const fstring smid(ps + beg, len);
			if (!comp(key, smid)) // smid <= key
				lo = mid + 1;
			else
				hi = mid;
		}
		return lo;
	}

	/// can be used to find common prefix ranges
	/// param: comp3 3-way compare, like strcmp
	template<class KeyPrefixCompare3>
	std::pair<size_t, size_t>
	equal_range3(const fstring key, const KeyPrefixCompare3& comp3) const {
		TERARK_VERIFY_EQ(en_sort_by_key, sort_flag);
		const Node* pn = &pNodes[0];
		const char* ps = strpool;
		size_t lo = 0, hi = nNodes;
		size_t mid0 = hi;
		while (lo < hi) {
			mid0 = lo + (hi - lo) / 2; // avoid overflow
			size_t beg = LOAD_OFFSET(pn[mid0+0].offset);
			size_t End = LOAD_OFFSET(pn[mid0+1].offset);
			size_t len = End - beg - extralen(ps, End);
			const fstring smid0(ps + beg, len);
			int ret = comp3(smid0, key);
			if (ret < 0)
				lo = mid0 + 1;
			else if (ret > 0)
				hi = mid0;
			else
				break;
		}
		size_t loH = mid0;
		while (lo < loH) {
			size_t mid = lo + (loH - lo) / 2; // avoid overflow
			size_t beg = LOAD_OFFSET(pn[mid+0].offset);
			size_t End = LOAD_OFFSET(pn[mid+1].offset);
			size_t len = End - beg - extralen(ps, End);
			const fstring smid(ps + beg, len);
			int ret = comp3(smid, key);
			if (ret < 0) // lower_bound
				lo = mid + 1;
			else
				loH = mid;
		}
		size_t hiL = mid0;
		while (hiL < hi) {
			size_t mid = hiL + (hi - hiL) / 2; // avoid overflow
			size_t beg = LOAD_OFFSET(pn[mid+0].offset);
			size_t End = LOAD_OFFSET(pn[mid+1].offset);
			size_t len = End - beg - extralen(ps, End);
			const fstring smid(ps + beg, len);
			int ret = comp3(smid, key);
			if (ret <= 0) // upper_bound
				hiL = mid + 1;
			else
				hi = mid;
		}
		return std::pair<size_t, size_t>(lo, hi);
	}
	std::pair<size_t, size_t>
	equal_range3(const char* prefix, size_t prefixlen) const {
		return equal_range3(fstring(prefix, prefixlen), fstring_func::prefix_compare3(prefixlen));
	}

// sort and binary search by value...
private:
	template<class CompareValue>
	struct SortCompareValueInline : CompareValue {
		bool operator()(const Node& x, const Node& y) const {
			return CompareValue::operator()(x.value, y.value);
		}
		SortCompareValueInline(CompareValue cmp) : CompareValue(cmp) {}
	};
	template<class CompareValue>
	struct SortCompareValueInlineByIndex : CompareValue {
		const Node* pn;
		bool operator()(const size_t x, const size_t y) const {
			return CompareValue::operator()(pn[x].value, pn[y].value);
		}
		SortCompareValueInlineByIndex(CompareValue cmp, const Node* pn) : CompareValue(cmp), pn(pn) {}
	};
	template<class CompareValue>
	struct SortCompareValueOut : CompareValue {
		const Value* pv;
		bool operator()(const size_t x, const size_t y) const {
			return CompareValue::operator()(pv[x], pv[y]);
		}
		SortCompareValueOut(CompareValue cmp, const Value* pv) : CompareValue(cmp), pv(pv) {}
	};

	template<class ValueCompare>
	void sort_by_value_impl(ValueCompare comp, ValueInline, sort_by_index_yes) {
		LinkTp* index = NULL;
		try { index = buildindex_by_int(); } catch (const std::bad_alloc&) {}
		if (index) {
			terark_parallel_sort(index, index + nNodes, SortCompareValueInlineByIndex<ValueCompare>(comp, pNodes));
			rearrange_nodes_by_int(index); // index will be free'ed
			rearrange_strpool(); // must after rearrange_nodes
			if (bucket) // is hash disabled?
				relink();
		}
		else // alloc fail, fall back to in-place sort
			sort_by_value_impl(comp, ValueInline(), sort_by_index_no());
	}
	template<class ValueCompare>
	void sort_by_value_impl(ValueCompare comp, ValueInline, sort_by_index_no) {
		terark_parallel_sort(pNodes, pNodes + nNodes, SortCompareValueInline<ValueCompare>(comp));
		rearrange_strpool(); // must after rearrange_nodes
		if (bucket) // is hash disabled?
			relink_fill();
	}
	template<class ValueCompare>
	void sort_by_value_impl(ValueCompare comp, ValueOut, sort_by_index_yes) {
		LinkTp* index = buildindex_by_int();
		Value* pv = values;
		terark_parallel_sort(index, index + nNodes, SortCompareValueOut<ValueCompare>(comp, pv));
		rearrange_nodes_by_int(index); // index will be free'ed
		rearrange_strpool(); // must after rearrange_nodes
		if (bucket) // is hash disabled?
			relink();
	}

	template<class KeyOfValue, class Compare>
	struct FindCompareValueInline : Compare {
		bool operator()(const KeyOfValue& x, const Node& y) const {
			return Compare::operator()(x, y.value);
		}
		bool operator()(const Node& x, const KeyOfValue& y) const {
			return Compare::operator()(x.value, y);
		}
		bool operator()(const Node& x, const Node& y) const {
			// in debug mode, vc will check order of sequence when binary search,
			// so this function is required for this purpose
			return Compare::operator()(x.value, y.value);
		}
		FindCompareValueInline(Compare comp) : Compare(comp) {}
	};

	template<class KeyOfValue, class Compare>
	size_t lower_bound_by_value_impl(KeyOfValue kov, Compare comp, ValueOut) const {
		const Value* pv = values;
		return std::lower_bound(pv, pv + nNodes, kov, comp) - pv;
	}
	template<class KeyOfValue, class Compare>
	size_t upper_bound_by_value_impl(KeyOfValue kov, Compare comp, ValueOut) const {
		const Value* pv = values;
		return std::upper_bound(pv, pv + nNodes, kov, comp) - pv;
	}
	template<class KeyOfValue, class Compare>
	size_t lower_bound_by_value_impl(KeyOfValue kov, Compare comp, ValueInline) const {
		const Node* pn = pNodes;
		return std::lower_bound(pn, pn + nNodes, kov, FindCompareValueInline<KeyOfValue, Compare>(comp)) - pn;
	}
	template<class KeyOfValue, class Compare>
	size_t upper_bound_by_value_impl(KeyOfValue kov, Compare comp, ValueInline) const {
		const Node* pn = pNodes;
		return std::upper_bound(pn, pn + nNodes, kov, FindCompareValueInline<KeyOfValue, Compare>(comp)) - pn;
	}

	template<class KeyOfValue, class Compare>
	std::pair<size_t, size_t>
	equal_range_by_value_impl(KeyOfValue kov, Compare comp, ValueOut) const {
		const Value* pv = values;
		std::pair<const Value*, const Value*> ii = std::equal_range(pv, pv + nNodes, kov, comp);
		return std::pair<size_t, size_t>(ii.first - pv, ii.second - pv);
	}
	template<class KeyOfValue, class Compare>
	std::pair<size_t, size_t>
	equal_range_by_value_impl(KeyOfValue kov, Compare comp, ValueInline) const {
		const Node* pn = pNodes;
		std::pair<const Node*, const Node*> ii =
		std::equal_range(pn, pn + nNodes, kov, FindCompareValueInline<KeyOfValue, Compare>(comp));
		return std::pair<size_t, size_t>(ii.first - pn, ii.second - pn);
	}
public:
	template<class ValueCompare>
	void sort_by_value(ValueCompare comp) {
		BOOST_STATIC_ASSERT(!is_value_empty);
		short old_fastleng = fastleng;
		if (nDeleted) {
			if constexpr (WithFreeList) {
				if (freelist_disabled != old_fastleng)
					disable_freelist();
			}
			revoke_deleted_no_relink();
		}
		if (nNodes >= 2) {
			save_strlen_to_link();
			sort_by_value_impl(comp, ValuePlace(), sort_by_index());
		}
		if constexpr (WithFreeList) {
			if (freelist_disabled != old_fastleng)
				enable_freelist(old_fastleng);
		}
		sort_flag = en_sort_by_val;
	}
	void sort_by_value() {
		BOOST_STATIC_ASSERT(!is_value_empty);
		sort_by_value(std::less<Value>());
	}
	template<class KeyOfValue, class Compare>
	size_t lower_bound_by_value(const KeyOfValue& kov, Compare comp) const {
		TERARK_VERIFY_EQ(en_sort_by_val, sort_flag);
		return lower_bound_by_value_impl(kov, comp, ValuePlace());
	}
	template<class KeyOfValue, class Compare>
	size_t upper_bound_by_value(const KeyOfValue& kov, Compare comp) const {
		TERARK_VERIFY_EQ(en_sort_by_val, sort_flag);
		return upper_bound_by_value_impl(kov, comp, ValuePlace());
	}
	template<class KeyOfValue, class Compare>
	std::pair<size_t, size_t>
	equal_range_by_value(const KeyOfValue& kov, Compare comp) const {
		TERARK_VERIFY_EQ(en_sort_by_val, sort_flag);
		return equal_range_by_value_impl(kov, comp, ValuePlace());
	}

	size_t lower_bound_by_value(const Value& val) const {
		TERARK_VERIFY_EQ(en_sort_by_val, sort_flag);
		return lower_bound_by_value_impl(val, std::less<Value>(), ValuePlace());
	}
	size_t upper_bound_by_value(const Value& val) const {
		TERARK_VERIFY_EQ(en_sort_by_val, sort_flag);
		return upper_bound_by_value_impl(val, std::less<Value>(), ValuePlace());
	}
	std::pair<size_t, size_t>
	equal_range_by_value(const Value& val) const {
		TERARK_VERIFY_EQ(en_sort_by_val, sort_flag);
		return equal_range_by_value_impl(val, std::less<Value>(), ValuePlace());
	}

// sort by composite key-value, code is somewhat longer, but simple
// k3v2 : first 3-way key compare, second 2-way val compare
// v2k2 : first 2-way val compare, second 2-way key compare
//
private:
// key first, value second, 3-way key compare
	template<class KeyCmp3, class ValCmp2>
	struct cmp_by_index_ValueOut_k3v2 { // such as case insensitive compare
		bool operator()(const KeyIndex& x, const KeyIndex& y) const {
			const fstring sx(ps + LOAD_OFFSET(x.offset), x.length);
			const fstring sy(ps + LOAD_OFFSET(y.offset), y.length);
			int ret = kc(sx, sy);
			if (ret != 0)
				return ret < 0;
			return vc(pv[x.idx], pv[y.idx]);
		}
		cmp_by_index_ValueOut_k3v2(const char* ps, const Value* pv, KeyCmp3 kc, ValCmp2 vc)
			: ps(ps), pv(pv), kc(kc), vc(vc) {}
		const char*  ps;
		const Value* pv;
		KeyCmp3      kc;
		ValCmp2      vc;
	};
	template<class KeyCmp3, class ValCmp2>
	struct cmp_by_index_ValueInline_k3v2 {
		bool operator()(const KeyIndex& x, const KeyIndex& y) const {
			const fstring sx(ps + LOAD_OFFSET(x.offset), x.length);
			const fstring sy(ps + LOAD_OFFSET(y.offset), y.length);
			int ret = kc(sx, sy);
			if (ret != 0)
				return ret < 0;
			return pn[x.idx].value < pn[y.idx].value;
		}
		cmp_by_index_ValueInline_k3v2(const char* ps, const Node* pn, KeyCmp3 kc, ValCmp2 vc)
			: ps(ps), pn(pn), kc(kc), vc(vc) {}
		const char* ps;
		const Node* pn;
		KeyCmp3     kc;
		ValCmp2     vc;
	};
	template<class KeyCmp3, class ValCmp2>
	struct cmp_ValueInline_k3v2 {
		bool operator()(const Node& x, const Node& y) const {
			fstring sx(ps + LOAD_OFFSET(x.offset), x.link); // link is length
			fstring sy(ps + LOAD_OFFSET(y.offset), y.link);
			int ret = kc(sx, sy);
			if (ret != 0)
				return ret < 0;
			return vc(x.value, y.value);
		}
		cmp_ValueInline_k3v2(const char* ps, KeyCmp3 kc, ValCmp2 vc)
			: ps(ps), kc(kc), vc(vc) {}
		const char* ps;
		KeyCmp3     kc;
		ValCmp2     vc;
	};
	template<class KeyCmp3, class ValCmp2>
	void sort_k3v2_(KeyCmp3 kc, ValCmp2 vc, sort_by_index_yes, ValueOut) {
		KeyIndex* index = buildindex();
		terark_parallel_sort(index, index + nNodes,
			cmp_by_index_ValueOut_k3v2<KeyCmp3, ValCmp2>(strpool, values, kc, vc));
		rearrange_nodes(index);
		rearrange_strpool(); // must after rearrange_nodes
		if (bucket) // is hash disabled?
			relink();
	}
	template<class KeyCmp3, class ValCmp2>
	void sort_k3v2_(KeyCmp3 kc, ValCmp2 vc, sort_by_index_yes, ValueInline) {
		KeyIndex* index = buildindex();
		terark_parallel_sort(index, index + nNodes,
			cmp_by_index_ValueInline_k3v2<KeyCmp3, ValCmp2>(strpool, pNodes, kc, vc));
		rearrange_nodes(index);
		rearrange_strpool(); // must after rearrange_nodes
		if (bucket) // is hash disabled?
			relink();
	}
	template<class KeyCmp3, class ValCmp2>
	void sort_k3v2_(KeyCmp3 kc, ValCmp2 vc,	sort_by_index_no, ValueInline) {
		save_strlen_to_link();
		terark_parallel_sort(pNodes, pNodes + nNodes, cmp_ValueInline_k3v2<KeyCmp3, ValCmp2>(strpool, kc, vc));
		rearrange_strpool(); // must after rearrange_nodes
		if (bucket) // is hash disabled?
			relink_fill();
	}
public:
	template<class KeyCmp3, class ValCmp2>
	void sort_k3v2(KeyCmp3 kc, ValCmp2 vc) {
		BOOST_STATIC_ASSERT(!is_value_empty);
		if (nDeleted)
			revoke_deleted_no_relink();
		if (nNodes >= 2) {
			sort_k3v2_(kc, vc, sort_by_index(), ValuePlace());
		}
		sort_flag = en_sort_by_key;
	}

    void i_know_data_is_sorted() {
		sort_flag = en_sort_by_key;
    }

private:
// value first, key second, 2-way key compare, 2-way value compare
	template<class KeyCmp2, class ValCmp2>
	struct cmp_by_index_ValueOut_v2k2 {
		bool operator()(const KeyIndex& x, const KeyIndex& y) const {
			const Value& vx = pv[x.idx];
			const Value& vy = pv[y.idx];
			if (vc(vx, vy))
				return true;
			else if (vc(vy, vx))
				return false;
			else {
				const fstring sx(ps + LOAD_OFFSET(x.offset), x.length);
				const fstring sy(ps + LOAD_OFFSET(y.offset), y.length);
				return kc(sx, sy);
			}
		}
		cmp_by_index_ValueOut_v2k2(const char* ps, const Value* pv, KeyCmp2 kc, ValCmp2 vc)
			: ps(ps), pv(pv), kc(kc), vc(vc) {}
		const char*  ps;
		const Value* pv;
		KeyCmp2      kc;
		ValCmp2      vc;
	};
	template<class KeyCmp2, class ValCmp2>
	struct cmp_by_index_ValueInline_v2k2 {
		bool operator()(const KeyIndex& x, const KeyIndex& y) const {
			const Value& vx = pn[x.idx].value;
			const Value& vy = pn[y.idx].value;
			if (vc(vx, vy))
				return true;
			else if (vc(vy, vx))
				return false;
			else {
				const fstring sx(ps + LOAD_OFFSET(x.offset), x.length);
				const fstring sy(ps + LOAD_OFFSET(y.offset), y.length);
				return kc(sx, sy);
			}
		}
		cmp_by_index_ValueInline_v2k2(const char* ps, const Node* pn, KeyCmp2 kc, ValCmp2 vc)
			: ps(ps), pn(pn), kc(kc), vc(vc) {}
		const char* ps;
		const Node* pn;
		KeyCmp2     kc;
		ValCmp2     vc;
	};
	template<class KeyCmp2, class ValCmp2>
	struct cmp_ValueInline_v2k2 {
		bool operator()(const Node& x, const Node& y) const {
			if (vc(x.value, y.value))
				return true;
			else if (vc(y.value, x.value))
				return false;
			else {
				const fstring sx(ps + LOAD_OFFSET(x.offset), x.link);
				const fstring sy(ps + LOAD_OFFSET(y.offset), y.link);
				return kc(sx, sy);
			}
		}
		cmp_ValueInline_v2k2(const char* ps, KeyCmp2 kc, ValCmp2 vc)
			: ps(ps), kc(kc), vc(vc) {}
		const char* ps;
		KeyCmp2     kc;
		ValCmp2     vc;
	};
	template<class KeyCmp2, class ValCmp2>
	void sort_v2k2_(KeyCmp2 kc, ValCmp2 vc, sort_by_index_yes, ValueOut) {
		KeyIndex* index = buildindex();
		terark_parallel_sort(index, index + nNodes,
			cmp_by_index_ValueOut_v2k2<KeyCmp2, ValCmp2>(strpool, values, kc, vc));
		rearrange_nodes(index);
		rearrange_strpool(); // must after rearrange_nodes
		if (bucket) // is hash disabled?
			relink();
	}
	template<class KeyCmp2, class ValCmp2>
	void sort_v2k2_(KeyCmp2 kc, ValCmp2 vc, sort_by_index_yes, ValueInline) {
		KeyIndex* index = NULL;
		try { index = buildindex(); } catch (const std::bad_alloc&) {}
		if (index) {
			terark_parallel_sort(index, index + nNodes,
				cmp_by_index_ValueInline_v2k2<KeyCmp2, ValCmp2>(strpool, pNodes, kc, vc));
			rearrange_nodes(index);
			rearrange_strpool(); // must after rearrange_nodes
			if (bucket) // is hash disabled?
				relink();
		}
		else // alloc fail, fall back to in-place sort
			sort_v2k2_(kc, vc, sort_by_index_no(), ValueInline());
	}
	template<class KeyCmp2, class ValCmp2>
	void sort_v2k2_(KeyCmp2 kc, ValCmp2 vc,	sort_by_index_no, ValueInline) {
		save_strlen_to_link();
		terark_parallel_sort(pNodes, pNodes + nNodes, cmp_ValueInline_v2k2<KeyCmp2, ValCmp2>(strpool, kc, vc));
		rearrange_strpool(); // must after rearrange_nodes
		if (bucket) // is hash disabled?
			relink_fill();
	}
public:
	template<class KeyCmp2, class ValCmp2>
	void sort_v2k2(ValCmp2 vc, KeyCmp2 kc) {
		BOOST_STATIC_ASSERT(!is_value_empty);
		if (nDeleted)
			revoke_deleted_no_relink();
		if (nNodes >= 2) {
			sort_v2k2_(kc, vc, sort_by_index(), ValuePlace());
		}
		sort_flag = en_sort_by_val;
	}
	template<class ValCmp2>
	void sort_v2k2(ValCmp2 vc) {
		sort_v2k2(vc, fstring_func::IF_SP_ALIGN(less_align, less_unalign)());
	}
	void sort_v2k2() {
		sort_v2k2(std::less<Value>(), fstring_func::IF_SP_ALIGN(less_align, less_unalign)());
	}

	template<class IntVec>
	void bucket_histogram(IntVec& hist) const {
		for (size_t i = 0, n = nBucket; i < n; ++i) {
			size_t listlen = 0;
			for (LinkTp j = bucket[i]; j != tail; j = pNodes[j].link)
				++listlen;
			if (hist.size() <= listlen)
				hist.resize(listlen+1);
			hist[listlen]++;
		}
	}

	size_t bucket_size() const { return nBucket; }

	template<class OtherHashStrMap>
	size_t intersection_size(const OtherHashStrMap& y) const {
		size_t num = 0;
		if (0 == this->delcnt() && 0 == y.delcnt()) {
			if (y.end_i() < this->end_i()) {
				for (size_t i = 0; i < y.end_i(); ++i)
					if (this->exists(y.key(i))) num++;
			} else {
				for (size_t i = 0; i < nNodes; ++i)
					if (y.exists(this->key(i))) num++;
			}
		} else {
			if (y.size() < this->size()) {
				for (size_t i = y.beg_i(); i < y.end_i(); i = y.next_i(i))
					if (this->exists(y.key(i))) num++;
			} else {
				for (size_t i = beg_i(); i < end_i(); i = next_i(i))
					if (y.exists(this->key(i))) num++;
			}
		}
		return num;
	}
	template<class OtherHashStrMap>
	size_t union_size(const OtherHashStrMap& y) const {
		return this->size() + y.size() - intersection_size(y);
	}

	template<class DataIO>
	void dio_load_fast(DataIO& dio) {
		typename DataIO::my_var_uint64_t Size;
		typename DataIO::my_var_uint64_t StrPoolSize;
		dio >> Size;
		dio >> StrPoolSize;
		dio >> load_factor;
		dio.load_is_undefined_for_hash_strmap();
	}
	template<class DataIO>
	void dio_save_fast(DataIO& dio) const {
		dio.load_is_undefined_for_hash_strmap();
	}

	// compatible format (compatible to std::map/std::vector ...)
	template<class DataIO>
	void dio_load(DataIO& dio) {
		typename DataIO::my_var_uint64_t Size;
		clear();
		dio >> Size;
		this->reserve(Size.t);
		std::string key;
		Value       val;
		for (size_t i = 0, n = Size.t; i < n; ++i) {
			dio >> key;
			dio >> val;
			this->insert_i(key, val);
		}
	}
	template<class DataIO>
	void dio_save(DataIO& dio) const {
		dio << typename DataIO::my_var_uint64_t(this->size());
		if (this->nDeleted) {
			for (size_t i = 0, n = this->end_i(); i < n; ++i) {
				if (!this->is_deleted(i)) {
					dio << this->key(i);
					dio << this->val(i);
				}
			}
		} else {
			for (size_t i = 0, n = this->end_i(); i < n; ++i) {
				dio << this->key(i);
				dio << this->val(i);
			}
		}
	}

	template<class DataIO>
	friend void DataIO_loadObject(DataIO& dio, hash_strmap& self) {
		self.dio_load(dio);
	}
	template<class DataIO>
	friend void DataIO_saveObject(DataIO& dio, const hash_strmap& self) {
		self.dio_save(dio);
	}

	template<class ValueTpl>
	static inline bool IsValueEqual(const ValueTpl& x, const ValueTpl& y) {
		return x == y;
	}
	static inline bool IsValueEqual(const ValueOut&, const ValueOut&) {
		return true;
	}
	friend bool operator==(const hash_strmap& x, const hash_strmap& y) {
		if (x.size() != y.size()) {
			return false;
		}
		const size_t x_end_i = x.end_i();
		const size_t y_end_i = y.end_i();
		for (size_t i = 0; i < x_end_i; ++i) {
			if (!x.is_deleted(i)) {
				const fstring key = x.key(i);
				const size_t j = y.find_i(key);
				if (j == y_end_i)
					return false;
				if (!IsValueEqual(x.val(i), y.val(j))) {
					return false;
				}
			}
		}
		return true;
	}
	friend bool operator!=(const hash_strmap& x, const hash_strmap& y) {
		return !(x == y);
	}
};

template< class Value
		, class HashFunc = fstring_func::IF_SP_ALIGN( hash_align,  hash)
		, class KeyEqual = fstring_func::IF_SP_ALIGN(equal_align, equal)
		, class Deleter = HSM_DefaultDeleter
		, class LinkTp = unsigned int // could be unsigned short for small map
		>
class hash_strmap_p : public terark_ptr_hash_map
	< hash_strmap<Value*, HashFunc, KeyEqual, ValueInline, FastCopy, LinkTp>, Value*, Deleter>
{};

// replace all "std::unordered_map" as fast_hash_strmap in your source code
// which Key is std::string, you will get the advantage of hash_strmap
// if you don't want to use hash_strmap, just define the macro:
#ifdef DONT_USE_TERARK_fast_hash_strmap
	#define fast_hash_strmap std::unordered_map
#else
template< class Key // dummy for compatible with unordered_map<string,Value>
		, class Value
		, class KeyHash = fstring_func::IF_SP_ALIGN(hash_align, hash)
		, class KeyEqual = fstring_func::IF_SP_ALIGN(equal_align, equal)
		, class ValuePlace = typename boost::mpl::if_c<
					!boost::is_empty<Value>::value && // ValueInline should be non-empty
					sizeof(Value) % sizeof(intptr_t) == 0 && sizeof(Value) <= 32
				, ValueInline
				, ValueOut // If Value is empty, ValueOut will not use memory
			>::type
		, class CopyStrategy = FastCopy
		, class LinkTp = unsigned int // could be unsigned short for small map
		, class HashTp = HSM_HashTp
		>
class fast_hash_strmap : public
	hash_strmap<Value,KeyHash,KeyEqual,ValuePlace,CopyStrategy,LinkTp,HashTp>
{
	typedef
	hash_strmap<Value,KeyHash,KeyEqual,ValuePlace,CopyStrategy,LinkTp,HashTp>
	super;
	BOOST_STATIC_ASSERT((boost::is_same<Key, std::string>::value));
public:
	explicit
	fast_hash_strmap(KeyHash hash = KeyHash(), KeyEqual eq = KeyEqual())
	  : super(hash, eq) {}
	explicit
	fast_hash_strmap(size_t cap, KeyHash hash=KeyHash(), KeyEqual eq=KeyEqual())
	  : super(cap, hash, eq) {}

	template<class DataIO>
	friend void DataIO_loadObject(DataIO& dio, fast_hash_strmap& self) {
		self.dio_load(dio);
	}
	template<class DataIO>
	friend void DataIO_saveObject(DataIO& dio, const fast_hash_strmap& self) {
		self.dio_save(dio);
	}
};
#endif

} // namespace terark

///////////////////////////////////////////////////////////////////////////////

namespace std { // for std::swap

template< class Value
		, class HashFunc
		, class KeyEqual
		, class ValuePlace
		, class CopyStrategy
		, class LinkTp
		, class HashTp
		>
void
swap(terark::hash_strmap<Value, HashFunc, KeyEqual, ValuePlace, CopyStrategy, LinkTp, HashTp> &x,
	 terark::hash_strmap<Value, HashFunc, KeyEqual, ValuePlace, CopyStrategy, LinkTp, HashTp> &y)
{
	x.swap(y);
}

#ifdef DONT_USE_TERARK_fast_hash_strmap
#else
template< class Key
		, class Value
		, class HashFunc
		, class KeyEqual
		, class ValuePlace
		, class CopyStrategy
		, class LinkTp
		, class HashTp
		>
void
swap(terark::fast_hash_strmap<Key, Value, HashFunc, KeyEqual, ValuePlace, CopyStrategy, LinkTp, HashTp> &x,
	 terark::fast_hash_strmap<Key, Value, HashFunc, KeyEqual, ValuePlace, CopyStrategy, LinkTp, HashTp> &y)
{
	x.swap(y);
}
#endif

} // namespace std

#if defined(__GNUC__) && __GNUC_MINOR__ + 1000 * __GNUC__ > 7000
  #pragma GCC diagnostic pop
#endif
