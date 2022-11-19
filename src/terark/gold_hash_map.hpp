#pragma once

#ifdef _MSC_VER
	#if _MSC_VER < 1600
		#include <hash_map>
		#define DEFAULT_HASH_FUNC stdext::hash
	#else
	 	#include <unordered_map>
	 	#define DEFAULT_HASH_FUNC std::hash
	#endif
#elif defined(__GNUC__) && __GNUC__ * 1000 + __GNUC_MINOR__ < 4001
	#include <ext/hash_map>
	#define DEFAULT_HASH_FUNC __gnu_cxx::hash
#elif defined(__GXX_EXPERIMENTAL_CXX0X__) || __cplusplus >= 201103 || defined(_LIBCPP_VERSION)
	#include <unordered_map>
	#define DEFAULT_HASH_FUNC std::hash
#else
	#include <tr1/unordered_map>
	#define DEFAULT_HASH_FUNC std::tr1::hash
#endif

#include <terark/parallel_lib.hpp>
#include "hash_common.hpp"
#include <utility> // for std::identity
#include <terark/util/function.hpp> // for reference_wrapper
#include <boost/current_function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/type_traits/has_trivial_constructor.hpp>
#include <boost/type_traits/has_trivial_destructor.hpp>

#if defined(__GNUC__) && __GNUC_MINOR__ + 1000 * __GNUC__ > 7000
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wclass-memaccess" // which version support?
  #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif

namespace terark {

template< class Key
		, class Elem = Key
		, class HashEqual = hash_and_equal<Key, DEFAULT_HASH_FUNC<Key>, std::equal_to<Key> >
		, class KeyExtractor = terark_identity<Elem>
		, class NodeLayout = node_layout<Elem, unsigned/*LinkTp*/ >
		, class HashTp = size_t
		>
class gold_hash_tab : dummy_bucket<typename NodeLayout::link_t>, HashEqual, KeyExtractor
{
#define MyKeyExtractor static_cast<const KeyExtractor&>(*this)
protected:
	typedef typename NodeLayout::link_t LinkTp;
	typedef typename NodeLayout::copy_strategy CopyStrategy;

	using dummy_bucket<LinkTp>::tail;
	using dummy_bucket<LinkTp>::delmark;
	static const intptr_t hash_cache_disabled = 8; // good if compiler aggressive optimize

	struct IsNotFree {
		bool operator()(LinkTp link_val) const { return delmark != link_val; }
	};

public:
	typedef typename ParamPassType<Key>::type         key_param_pass_t;

	typedef typename NodeLayout::      iterator       fast_iterator;
	typedef typename NodeLayout::const_iterator const_fast_iterator;
	typedef std::reverse_iterator<      fast_iterator>       fast_reverse_iterator;
	typedef std::reverse_iterator<const_fast_iterator> const_fast_reverse_iterator;

#ifdef TERARK_GOLD_HASH_MAP_ITERATOR_USE_FAST
	typedef       fast_iterator       iterator;
	typedef const_fast_iterator const_iterator;
	      iterator get_iter(size_t idx)       { return m_nl.begin() + idx; }
	const_iterator get_iter(size_t idx) const { return m_nl.begin() + idx; }
#else
	class iterator; friend class iterator;
	class iterator {
	public:
		typedef Elem  value_type;
		typedef gold_hash_tab* OwnerPtr;
	#define ClassIterator iterator
	#include "gold_hash_map_iterator.hpp"
	};
	class const_iterator; friend class const_iterator;
	class const_iterator {
	public:
		typedef const Elem  value_type;
		const_iterator(const iterator& y)
		  : owner(y.get_owner()), index(y.get_index()) {}
		typedef const gold_hash_tab* OwnerPtr;
	#define ClassIterator const_iterator
	#include "gold_hash_map_iterator.hpp"
	};
	      iterator get_iter(size_t idx)       { return       iterator(this, idx); }
	const_iterator get_iter(size_t idx) const { return const_iterator(this, idx); }
#endif
	typedef std::reverse_iterator<      iterator>       reverse_iterator;
	typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

	typedef ptrdiff_t difference_type;
	typedef size_t  size_type;
	typedef Elem  value_type;
	typedef Elem& reference;

	typedef const Key key_type;
	typedef LinkTp link_t;

protected:
	NodeLayout m_nl;
	LinkTp* bucket;  // index to m_nl
	HashTp* pHash;

	double  load_factor;
	size_t  nBucket; // may larger than LinkTp.max if LinkTp is small than size_t
	LinkTp  nElem;
	LinkTp  maxElem;
	LinkTp  maxload;  // cached guard for calling rehash
	LinkTp  freelist_head;
	LinkTp  freelist_size;
    LinkTp  freelist_freq;
	bool    is_sorted;
	bool    m_enable_auto_gc; // will not keep stable index(on elem erased)

private:
	void init() {
		BOOST_STATIC_ASSERT(sizeof(LinkTp) <= sizeof(Elem));
		new(&m_nl)NodeLayout();
		nElem = 0;
		maxElem = 0;
		maxload = 0;
		if (!boost::has_trivial_destructor<Elem>::value || sizeof(Elem) > sizeof(intptr_t))
			pHash = NULL;
		else
			pHash = (HashTp*)(hash_cache_disabled);

		bucket = const_cast<LinkTp*>(&tail); // false start
		nBucket = 1;
		freelist_head = tail; // empty freelist
		freelist_size = 0;
        freelist_freq = 0;

		load_factor = 0.8;
		is_sorted = true;
		m_enable_auto_gc = false;
	}

	void relink_impl(bool bFillHash) {
		LinkTp* pb = bucket;
		size_t  nb = nBucket;
		NodeLayout nl = m_nl;
		// (LinkTp)tail is just used for coerce tail be passed by value
		// otherwise, tail is passed by reference, this cause g++ link error
		if (nb > 1) {
			assert(&tail != pb);
			std::fill_n(pb, nb, (LinkTp)tail);
		} else {
			assert(&tail == pb);
			return;
		}
		{ // set delmark
			LinkTp i = freelist_head;
			while (i < delmark) {
				nl.link(i) = delmark;
				i = reinterpret_cast<LinkTp&>(nl.data(i));
			}
		}
		if (intptr_t(pHash) == hash_cache_disabled) {
			if (0 == freelist_size)
				for (size_t j = 0, n = nElem; j < n; ++j) {
					size_t i = HashEqual::hash(MyKeyExtractor(nl.data(j))) % nb;
					nl.link(j) = pb[i];
					pb[i] = LinkTp(j);
				}
			else
				for (size_t j = 0, n = nElem; j < n; ++j) {
					if (delmark != nl.link(j)) {
						size_t i = HashEqual::hash(MyKeyExtractor(nl.data(j))) % nb;
						nl.link(j) = pb[i];
						pb[i] = LinkTp(j);
					}
				}
		}
		else if (bFillHash) {
			HashTp* ph = pHash;
			if (0 == freelist_size)
				for (size_t j = 0, n = nElem; j < n; ++j) {
					HashTp h = HashTp(HashEqual::hash(MyKeyExtractor(nl.data(j))));
					size_t i = h % nb;
					ph[j] = h;
					nl.link(j) = pb[i];
					pb[i] = LinkTp(j);
				}
			else
				for (size_t j = 0, n = nElem; j < n; ++j) {
					if (delmark != nl.link(j)) {
						HashTp h = HashTp(HashEqual::hash(MyKeyExtractor(nl.data(j))));
						size_t i = h % nb;
						ph[j] = h;
						nl.link(j) = pb[i];
						pb[i] = LinkTp(j);
					}
				}
		}
        else {
			const HashTp* ph = pHash;
			if (0 == freelist_size)
				for (size_t j = 0, n = nElem; j < n; ++j) {
					size_t i = ph[j] % nb;
					nl.link(j) = pb[i];
					pb[i] = LinkTp(j);
				}
			else
				for (size_t j = 0, n = nElem; j < n; ++j) {
					if (delmark != nl.link(j)) {
						size_t i = ph[j] % nb;
						nl.link(j) = pb[i];
						pb[i] = LinkTp(j);
					}
				}
        }
	}

	void relink() { relink_impl(false); }
	void relink_fill() { relink_impl(true); }

	void destroy() {
		NodeLayout nl = m_nl;
		if (!nl.is_null()) {
			if (!boost::has_trivial_destructor<Elem>::value) {
                if (freelist_is_empty()) {
                    for (size_t i = nElem; i > 0; --i)
                        nl.data(i-1).~Elem();
                } else {
                    for (size_t i = nElem; i > 0; --i)
                        if (delmark != nl.link(i-1))
                            nl.data(i-1).~Elem();
                }
			}
			m_nl.free();
		}
		if (intptr_t(pHash) != hash_cache_disabled && NULL != pHash)
			free(pHash);
		if (bucket && &tail != bucket)
			free(bucket);
	}

public:
	explicit gold_hash_tab(HashEqual he = HashEqual()
                         , KeyExtractor keyExtr = KeyExtractor())
	  : HashEqual(he), KeyExtractor(keyExtr) {
		init();
	}
	explicit gold_hash_tab(size_t cap
						 , HashEqual he = HashEqual()
						 , KeyExtractor keyExtr = KeyExtractor())
	  : HashEqual(he), KeyExtractor(keyExtr) {
		init();
		reserve(cap);
	}
	gold_hash_tab(std::initializer_list<Elem> list
				, HashEqual he = HashEqual()
				, KeyExtractor keyExtr = KeyExtractor())
	  : HashEqual(he), KeyExtractor(keyExtr) {
		init();
		reserve(list.size());
		for (auto& e : list)
			this->insert_i(e);
	}
	/// ensured not calling HashEqual and KeyExtractor
	gold_hash_tab(const gold_hash_tab& y)
	  : HashEqual(y), KeyExtractor(y) {
		nElem = y.nElem;
		maxElem = y.nElem;
		maxload = y.maxload;
		nBucket = y.nBucket;
		pHash = NULL;
		freelist_head = y.freelist_head;
		freelist_size = y.freelist_size;
        freelist_freq = y.freelist_freq;

		load_factor = y.load_factor;
		is_sorted = y.is_sorted;

		if (0 == nElem) { // empty
			nBucket = 1;
			maxload = 0;
			bucket  = const_cast<LinkTp*>(&tail);
			return; // not need to do deep copy
		}
		assert(y.bucket != &tail);
		m_nl.reserve(0, nElem);
		bucket = (LinkTp*)malloc(sizeof(LinkTp) * y.nBucket);
		if (NULL == bucket) {
			m_nl.free();
			init();
			TERARK_DIE("malloc(%zd)", sizeof(LinkTp) * y.nBucket);
		}
		if (intptr_t(y.pHash) == hash_cache_disabled) {
			pHash = (HashTp*)(hash_cache_disabled);
		}
		else {
			pHash = (HashTp*)malloc(sizeof(HashTp) * nElem);
			if (NULL == pHash) {
				free(bucket);
				m_nl.free();
				TERARK_DIE("malloc(%zd)", sizeof(HashTp) * nElem);
			}
			memcpy(pHash, y.pHash, sizeof(HashTp) * nElem);
		}
		memcpy(bucket, y.bucket, sizeof(LinkTp) * nBucket);
		if (!boost::has_trivial_copy<Elem>::value
				&& freelist_size) {
			node_layout_copy_cons(m_nl, y.m_nl, nElem, IsNotFree());
		} else // freelist is empty, copy all
			node_layout_copy_cons(m_nl, y.m_nl, nElem);
	}
	gold_hash_tab& operator=(const gold_hash_tab& y) {
		if (this != &y) {
		#if 1 // prefer performance, std::map is in this manner
		// this is exception-safe but not transactional
        // non-transactional: lose old content of *this on exception
			this->destroy();
			new(this)gold_hash_tab(y);
		#else // exception-safe, but take more peak memory
			gold_hash_tab(y).swap(*this);
		#endif
		}
		return *this;
	}
	gold_hash_tab(gold_hash_tab&& y) noexcept
	  : HashEqual(std::move(y)), KeyExtractor(std::move(y)) {
		pHash   =  y.pHash;
		m_nl    =  y.m_nl;
		nElem   =  y.nElem;
		maxElem =  y.maxElem;
		maxload =  y.maxload;

		nBucket =  y.nBucket;
		bucket  =  y.bucket;

		freelist_head =  y.freelist_head;
		freelist_size =  y.freelist_size;
		freelist_freq =  y.freelist_freq;

		load_factor =  y.load_factor;
		is_sorted   =  y.is_sorted;

		y.init(); // reset y as empty
	}
	gold_hash_tab& operator=(gold_hash_tab&& y) noexcept {
		if (this != &y) {
			this->~gold_hash_tab();
			new(this)gold_hash_tab(std::move(y));
		}
		return *this;
	}
	~gold_hash_tab() { destroy(); }

	void swap(gold_hash_tab& y) {
		std::swap(pHash  , y.pHash);
		std::swap(m_nl   , y.m_nl);
		std::swap(nElem  , y.nElem);
		std::swap(maxElem, y.maxElem);
		std::swap(maxload, y.maxload);

		std::swap(nBucket, y.nBucket);
		std::swap(bucket , y.bucket);

		std::swap(freelist_head, y.freelist_head);
		std::swap(freelist_size, y.freelist_size);
		std::swap(freelist_freq, y.freelist_freq);

		std::swap(load_factor, y.load_factor);
		std::swap(is_sorted  , y.is_sorted);
		std::swap(static_cast<HashEqual&>(*this), static_cast<HashEqual&>(y));
		std::swap(static_cast<KeyExtractor&>(*this) , static_cast<KeyExtractor&>(y));
	}

//	void setHashEqual(const HashEqual& he) { this->he = he; }
//	void setKeyExtractor(const KeyExtractor& kEx) { this->getKeyExtractor() = kEx; }

	const HashEqual& getHashEqual() const { return *this; }
	const KeyExtractor& getKeyExtractor() const { return *this; }

	void clear() {
		destroy();
		init();
	}

	void shrink_to_fit() { if (nElem < maxElem) reserve(nElem); }

	bool is_hash_cached() const {
		return intptr_t(pHash) != hash_cache_disabled;
	}

	void enable_hash_cache() {
		if (intptr_t(pHash) == hash_cache_disabled) {
			if (0 == maxElem) {
				pHash = NULL;
			}
			else {
				HashTp* ph = (HashTp*)malloc(sizeof(HashTp) * maxElem);
				if (NULL == ph) {
					TERARK_DIE("malloc(%zd)", sizeof(HashTp) * maxElem);
				}
				if (0 == freelist_size) {
					for (size_t i = 0, n = nElem; i < n; ++i)
						ph[i] = HashEqual::hash(MyKeyExtractor(m_nl.data(i)));
				}
				else {
					for (size_t i = 0, n = nElem; i < n; ++i)
						if (delmark != m_nl.link(i))
							ph[i] = HashEqual::hash(MyKeyExtractor(m_nl.data(i)));
				}
				pHash = ph;
			}
		}
		TERARK_VERIFY_F(nullptr != pHash || 0 == maxElem,
						"%p : %zd", pHash, size_t(maxElem));
	}

	void disable_hash_cache() {
		if (intptr_t(pHash) == hash_cache_disabled)
			return;
		if (pHash)
			free(pHash);
		pHash = (HashTp*)(hash_cache_disabled);
	}

	size_t rehash(size_t newBucketSize) {
		newBucketSize = __hsm_stl_next_prime(newBucketSize);
		TERARK_VERIFY_GE(newBucketSize, 5);
		if (newBucketSize != nBucket) {
			// shrink or enlarge
			// "tail" has multi impl in diff dll/so
			// so we should not if (bucket != &tail)
			if (nBucket != 1) {
				TERARK_VERIFY(nullptr != bucket);
				free(bucket);
			}
			else {
				TERARK_VERIFY_EQ(tail, *bucket);
			}
			bucket = (LinkTp*)malloc(sizeof(LinkTp) * newBucketSize);
			TERARK_VERIFY_F(nullptr != bucket, "malloc(%zd) = NULL",
							sizeof(LinkTp) * newBucketSize);
			nBucket = newBucketSize;
			relink();
			maxload = LinkTp(newBucketSize * load_factor);
		}
		return newBucketSize;
	}
//	void resize(size_t n) { rehash(n); }

	void reserve(size_t cap) {
		reserve_nodes(cap);
		rehash(size_t(cap / load_factor + 1));
	}

	void reserve_nodes(size_t cap) {
	//	TERARK_VERIFY_GE(cap, maxElem); // could be shrink
		TERARK_VERIFY_GE(cap, nElem);
		TERARK_VERIFY_LE(cap, delmark);
		if (cap != (size_t)maxElem && cap != nElem) {
			if (intptr_t(pHash) != hash_cache_disabled) {
				HashTp* ph = (HashTp*)realloc(pHash, sizeof(HashTp) * cap);
				if (NULL == ph)
					TERARK_DIE("malloc(%zd)", sizeof(HashTp) * cap);
				pHash = ph;
			}
			if (freelist_size)
				m_nl.reserve(nElem, cap, IsNotFree());
			else
				m_nl.reserve(nElem, cap);
			maxElem = LinkTp(cap);
		}
	}

	void set_load_factor(double fact) {
		if (fact >= 0.999) {
			throw std::logic_error("load factor must <= 0.999");
		}
		load_factor = fact;
		maxload = &tail == bucket ? 0 : LinkTp(nBucket * fact);
	}
	double get_load_factor() const { return load_factor; }

	inline HashTp hash_i(size_t i) const {
		TERARK_ASSERT_LT(i, nElem);
		if (intptr_t(pHash) == hash_cache_disabled)
			return HashTp(HashEqual::hash(MyKeyExtractor(m_nl.data(i))));
		else
			return pHash[i];
	}
    inline HashTp hash_v(const Elem& e) const {
        return HashTp(HashEqual::hash(MyKeyExtractor(e)));
    }
	bool   empty() const { return nElem == freelist_size; }
	size_t  size() const { return nElem -  freelist_size; }
	size_t beg_i() const {
		size_t i;
		if (freelist_size == nElem)
			i = nElem;
		else if (freelist_size && delmark == m_nl.link(0))
			i = next_i(0);
		else
			i = 0;
		return i;
	}
	size_t  end_i() const { return nElem; }
	size_t rbeg_i() const { return nElem; }
	size_t rend_i() const { return 0; }
	size_t next_i(size_t idx) const {
		size_t n = nElem;
		TERARK_ASSERT_LT(idx, n);
		do ++idx; while (idx < n && delmark == m_nl.link(idx));
		return idx;
	}
	size_t prev_i(size_t idx) const {
		TERARK_ASSERT_GT(idx, 0);
		TERARK_ASSERT_LE(idx, nElem);
		do --idx; while (idx > 0 && delmark == m_nl.link(idx));
		return idx;
	}
	size_t max_size() const { return size_t(-1); }
	size_t capacity() const { return maxElem; }
	size_t delcnt() const { return freelist_size; }

	      iterator  begin()       { return get_iter(beg_i()); }
	const_iterator  begin() const { return get_iter(beg_i()); }
	const_iterator cbegin() const { return get_iter(beg_i()); }

	      iterator  end()       { return get_iter(nElem); }
	const_iterator  end() const { return get_iter(nElem); }
	const_iterator cend() const { return get_iter(nElem); }

	      reverse_iterator  rbegin()       { return       reverse_iterator(end()); }
	const_reverse_iterator  rbegin() const { return const_reverse_iterator(end()); }
	const_reverse_iterator crbegin() const { return const_reverse_iterator(end()); }

	      reverse_iterator  rend()       { return       reverse_iterator(begin()); }
	const_reverse_iterator  rend() const { return const_reverse_iterator(begin()); }
	const_reverse_iterator crend() const { return const_reverse_iterator(begin()); }

	      fast_iterator  fast_begin()       { return m_nl.begin(); }
	const_fast_iterator  fast_begin() const { return m_nl.begin(); }
	const_fast_iterator fast_cbegin() const { return m_nl.begin(); }

	      fast_iterator  fast_end()       { return m_nl.begin() + nElem; }
	const_fast_iterator  fast_end() const { return m_nl.begin() + nElem; }
	const_fast_iterator fast_cend() const { return m_nl.begin() + nElem; }

	      fast_reverse_iterator  fast_rbegin()       { return       fast_reverse_iterator(fast_end()); }
	const_fast_reverse_iterator  fast_rbegin() const { return const_fast_reverse_iterator(fast_end()); }
	const_fast_reverse_iterator fast_crbegin() const { return const_fast_reverse_iterator(fast_end()); }

	      fast_reverse_iterator  fast_rend()       { return       fast_reverse_iterator(fast_begin()); }
	const_fast_reverse_iterator  fast_rend() const { return const_fast_reverse_iterator(fast_begin()); }
	const_fast_reverse_iterator fast_crend() const { return const_fast_reverse_iterator(fast_begin()); }

	      iterator  iter_of(size_t idx)       { return       iterator(this, idx); }
	const_iterator  iter_of(size_t idx) const { return const_iterator(this, idx); }

	template<class CompatibleObject>
	std::pair<iterator, bool> insert(const CompatibleObject& obj) {
		std::pair<size_t, bool> ib = insert_i(obj);
		return std::pair<iterator, bool>(get_iter(ib.first), ib.second);
	}
	std::pair<iterator, bool> insert(const Elem& obj) {
		std::pair<size_t, bool> ib = insert_i(obj);
		return std::pair<iterator, bool>(get_iter(ib.first), ib.second);
	}

	template<class Arg1>
	std::pair<iterator, bool> emplace(const Arg1& a1) {
		std::pair<size_t, bool> ib = insert_i(value_type(a1));
		return std::pair<iterator, bool>(get_iter(ib.first), ib.second);
	}
	template<class Arg1, class Arg2>
	std::pair<iterator, bool> emplace(const Arg1& a1, const Arg2& a2) {
		std::pair<size_t, bool> ib = insert_i(value_type(a1, a2));
		return std::pair<iterator, bool>(get_iter(ib.first), ib.second);
	}
	template<class... _Valty>
	std::pair<iterator, bool> emplace(_Valty&&... _Val) {
		std::pair<size_t, bool> ib =
			insert_i(value_type(std::forward<_Valty>(_Val)...));
		return std::pair<iterator, bool>(get_iter(ib.first), ib.second);
	}

	      iterator find(key_param_pass_t key)       { return get_iter(find_i(key)); }
	const_iterator find(key_param_pass_t key) const { return get_iter(find_i(key)); }

	// keep memory
	void erase_all() {
		NodeLayout nl = m_nl;
		if (!nl.is_null()) {
			if (!boost::has_trivial_destructor<Elem>::value) {
                if (freelist_is_empty()) {
                    for (size_t i = nElem; i > 0; --i)
                        nl.data(i-1).~Elem();
                } else {
                    for (size_t i = nElem; i > 0; --i)
                        if (delmark != nl.link(i-1))
                            nl.data(i-1).~Elem();
                }
			}
		}
		if (nElem > freelist_size) {
			std::fill_n(bucket, nBucket, (LinkTp)tail);
			nElem = 0;
		}
		if (freelist_head < delmark) {
			TERARK_VERIFY_LT(freelist_head, nElem);
			freelist_head = tail;
			freelist_size = 0;
			freelist_freq = 0;
		}
	}

#ifndef TERARK_GOLD_HASH_MAP_ITERATOR_USE_FAST
	iterator erase(iterator iter) {
		TERARK_ASSERT_EQ(iter.get_owner(), this);
		TERARK_ASSERT_LT(iter.get_index(), nElem);
		assert(!m_nl.is_null());
		size_t idx = iter.get_index();
		erase_i(iter.get_index());
		size_t End = end_i();
		while (idx < End && is_deleted(idx)) {}
		return iterator(this, idx);
	}
#endif
	void erase(fast_iterator iter) {
		assert(!m_nl.is_null());
		TERARK_ASSERT_GE(iter - m_nl.begin(), 0);
		TERARK_ASSERT_LT(iter - m_nl.begin(), nElem);
		erase_i(iter - m_nl.begin());
	}
private:
	// return erased elements count
	template<class Predictor>
	size_t erase_if_impl(Predictor pred, FastCopy) {
		if (freelist_size)
			// this extra scan should be eliminated, by some way
			revoke_deleted_no_relink();
		NodeLayout nl = m_nl;
		size_t i = 0, n = nElem;
		for (; i < n; ++i)
			if (pred(nl.data(i)))
				goto DoErase;
		return 0;
	DoErase:
		HashTp* ph = pHash;
		nl.data(i).~Elem();
		if (intptr_t(ph) == hash_cache_disabled) {
			for (size_t j = i + 1; j != n; ++j)
				if (pred(nl.data(j)))
					nl.data(j).~Elem();
				else
					memcpy(&nl.data(i), &nl.data(j), sizeof(Elem)), ++i;
		} else {
			for (size_t j = i + 1; j != n; ++j)
				if (pred(nl.data(j)))
					nl.data(j).~Elem();
				else {
					ph[i] = ph[j];
					memcpy(&nl.data(i), &nl.data(j), sizeof(Elem)), ++i;
				}
		}
		nElem = i;
		if (0 == i) { // all elements are erased
			this->destroy();
			this->init();
		}
		return n - i; // erased elements count
	}

	template<class Predictor>
	size_t erase_if_impl(Predictor pred, SafeCopy) {
		if (freelist_size)
			// this extra scan should be eliminated, by some way
			revoke_deleted_no_relink();
		NodeLayout nl = m_nl;
		size_t i = 0, n = nElem;
		for (; i < n; ++i)
			if (pred(nl.data(i)))
				goto DoErase;
		return 0;
	DoErase:
		HashTp* ph = pHash;
		if (intptr_t(ph) == hash_cache_disabled) {
			for (size_t j = i + 1; j != n; ++j)
				if (!pred(nl.data(j)))
					boost::swap(nl.data(i), nl.data(j)), ++i;
		} else {
			for (size_t j = i + 1; j != n; ++j)
				if (!pred(nl.data(j))) {
					ph[i] = ph[j];
					boost::swap(nl.data(i), nl.data(j)), ++i;
				}
		}
		if (!boost::has_trivial_destructor<Elem>::value) {
			for (size_t j = i; j != n; ++j)
				nl.data(j).~Elem();
		}
		nElem = i;
		if (0 == i) { // all elements are erased
			this->destroy();
			this->init();
		}
		return n - i; // erased elements count
	}

	// when auto gc is enabled, elem index is not stable!
	// since we have deleted configurable freelist, newer elem inserted on
	// hole slot will have smaller index.
	void enable_auto_gc(bool enable = true) { m_enable_auto_gc = enable; }
	bool is_auto_gc_enabled() const { return m_enable_auto_gc; }

public:
	//@{
	//@brief erase_if
	/// will invalidate saved id/index/iterator
	/// if id needs to be keeped, call keepid_erase_if
	template<class Predictor>
	size_t erase_if(Predictor pred) {
		size_t nErased = erase_if_impl(pred, CopyStrategy());
		if (nElem * 2 <= maxElem)
			shrink_to_fit();
		else
			relink();
		return nErased;
	}
	template<class Predictor>
	size_t shrink_after_erase_if(Predictor pred) {
		size_t nErased = erase_if_impl(pred, CopyStrategy());
		shrink_to_fit();
		return nErased;
	}
	template<class Predictor>
	size_t no_shrink_after_erase_if(Predictor pred) {
		size_t nErased = erase_if_impl(pred, CopyStrategy());
		relink();
		return nErased;
	}
	//@}

	template<class Predictor>
	size_t keepid_erase_if(Predictor pred) {
		// should be much slower than erase_if
		size_t  n = nElem;
		size_t  erased = 0;
		NodeLayout nl = m_nl;
		if (0 == freelist_size) {
			TERARK_VERIFY_EQ(freelist_head, tail);
			for (size_t i = 0; i != n; ++i)
				if (pred(nl.data(i)))
					erase_i(i), ++erased;
		}
		else {
			TERARK_VERIFY_LT(freelist_head, n);
			for (size_t i = 0; i != n; ++i)
				if (delmark != nl.link(i) && pred(nl.data(i)))
					erase_i(i), ++erased;
		}
		return erased;
	}
	// if return non-zero, all permanent id/index are invalidated
	terark_no_inline
	size_t revoke_deleted() {
		if (freelist_size) {
			size_t erased = revoke_deleted_no_relink();
			relink();
			return erased;
		} else
			return 0;
	}
	bool is_deleted(size_t idx) const {
		TERARK_ASSERT_LT(idx, nElem);
		return delmark == m_nl.link(idx);
	}
	bool freelist_is_empty() const { return 0 == freelist_size; }

	template<class CompatibleObject>
	std::pair<size_t, bool> insert_i(const CompatibleObject& obj) {
		key_param_pass_t key = MyKeyExtractor(obj);
		return lazy_insert_elem_i(key, CopyConsFunc<CompatibleObject>(obj));
	}
	std::pair<size_t, bool> insert_i(const Elem& obj) {
		key_param_pass_t key = MyKeyExtractor(obj);
		return lazy_insert_elem_i(key, CopyConsFunc<Elem>(obj));
	}
	template<class ConsElem>
	std::pair<size_t, bool>
	lazy_insert_elem_i(key_param_pass_t key, ConsElem cons_elem) {
		const HashTp h = HashTp(HashEqual::hash(key));
		size_t i = h % nBucket;
		for (LinkTp p = bucket[i]; tail != p; p = m_nl.link(p)) {
			TERARK_ASSERT_LT(p, nElem);
			if (HashEqual::equal(key, MyKeyExtractor(m_nl.data(p))))
				return std::make_pair(p, false);
		}
		if (terark_unlikely(nElem - freelist_size >= maxload)) {
			// rehash will auto find next prime bucket size
			i = h % rehash(nBucket + 1);
		}
		size_t slot = risk_slot_alloc(); // here, no risk
		cons_elem(&m_nl.data(slot)); // must success
		m_nl.link(slot) = bucket[i]; // newer at head
		bucket[i] = LinkTp(slot); // new head of i'th bucket
		if (intptr_t(pHash) != hash_cache_disabled)
			pHash[slot] = h;
		is_sorted = false;
		return std::make_pair(slot, true);
	}
	template<class ConsElem, class PreInsert>
	std::pair<size_t, bool>
	lazy_insert_elem_i(key_param_pass_t key, ConsElem cons_elem,
					   PreInsert pre_insert) {
		const HashTp h = HashTp(HashEqual::hash(key));
		const size_t old_nBucket = nBucket;
		size_t i = size_t(h % old_nBucket);
		// this func is a copy-paste except old_nBucket and pre_insert
		for (LinkTp p = bucket[i]; tail != p; p = m_nl.link(p)) {
			TERARK_ASSERT_LT(p, nElem);
			if (HashEqual::equal(key, MyKeyExtractor(m_nl.data(p))))
				return std::make_pair(p, false);
		}
		if (!pre_insert(this)) {
			return std::make_pair(nElem, true);
		}
		if (terark_unlikely(nElem - freelist_size >= maxload)) {
			// rehash will auto find next prime bucket size
			i = h % rehash(nBucket + 1);
		}
		else if (terark_unlikely(nBucket != old_nBucket)) {
			i = h % nBucket;
		}
		size_t slot = risk_slot_alloc(); // here, no risk
		cons_elem(&m_nl.data(slot)); // must success
		m_nl.link(slot) = bucket[i]; // newer at head
		bucket[i] = LinkTp(slot); // new head of i'th bucket
		if (intptr_t(pHash) != hash_cache_disabled)
			pHash[slot] = h;
		is_sorted = false;
		return std::make_pair(slot, true);
	}

	///@{ low level operations
	/// the caller should pay the risk for gain
	///
	LinkTp risk_slot_alloc() {
		LinkTp slot;
		if (0 == freelist_size) {
			TERARK_ASSERT_EQ(freelist_head, tail);
			slot = nElem;
			if (terark_unlikely(nElem == maxElem))
				reserve_nodes(0 == nElem ? 1 : 2*nElem);
			TERARK_ASSERT_LT(nElem, maxElem);
			nElem++;
		} else {
			TERARK_ASSERT_LT(freelist_head, nElem);
			TERARK_ASSERT_EQ(m_nl.link(freelist_head), delmark);
			slot = freelist_head;
			freelist_size--;
			freelist_head = reinterpret_cast<LinkTp&>(m_nl.data(slot));
			TERARK_ASSERT_F(freelist_head < nElem || freelist_head == tail,
							"%zd %zd", size_t(freelist_head), size_t(nElem));
		}
		m_nl.link(slot) = tail; // for debug check&verify
		return slot;
	}

	/// precond: # Elem on slot is constructed
	///          # Elem on slot is not in the table:
	///                 it is not reached through 'bucket'
	/// effect: destory the Elem on slot then free the slot
	void risk_slot_free(size_t slot) {
		TERARK_ASSERT_LT(slot, nElem);
		TERARK_ASSERT_NE(delmark, m_nl.link(slot));
		fast_slot_free(slot);
	}

	/// @note when calling this function:
	//    1. this->begin()[slot] must has been newly constructed by the caller
	//    2. this->begin()[slot] must not existed in the hash index
	size_t risk_insert_on_slot(size_t slot) {
		TERARK_ASSERT_LT(slot, nElem);
		TERARK_ASSERT_NE(delmark, m_nl.link(slot));
		key_param_pass_t key = MyKeyExtractor(m_nl.data(slot));
		const HashTp h = HashTp(HashEqual::hash(key));
		size_t i = h % nBucket;
		for (LinkTp p = bucket[i]; tail != p; p = m_nl.link(p)) {
			TERARK_ASSERT_NE(p, slot); // slot must not be reached
			TERARK_ASSERT_LT(p, nElem);
			if (HashEqual::equal(key, MyKeyExtractor(m_nl.data(p))))
				return p;
		}
		if (intptr_t(pHash) != hash_cache_disabled)
			pHash[slot] = h;
		if (terark_unlikely(nElem - freelist_size >= maxload)) { // must be >=
			// rehash will set the bucket&link for 'slot'
			rehash(nBucket + 1); // will auto find next prime bucket size
		} else {
			m_nl.link(slot) = bucket[i]; // newer at head
			bucket[i] = LinkTp(slot); // new head of i'th bucket
		}
		is_sorted = false;
		return slot;
	}

	void risk_size_inc(LinkTp n) {
		if (terark_unlikely(nElem + n > maxElem))
			reserve_nodes(nElem + std::max(nElem, n));
		TERARK_VERIFY_LE(nElem + n, maxElem);
		NodeLayout nl = m_nl;
		for (size_t i = n, j = nElem; i; --i, ++j)
			nl.link(j) = (LinkTp)tail;
		nElem += n;
	}

	void risk_size_dec(LinkTp n) {
		TERARK_VERIFY_GE(nElem, n);
		nElem -= n;
	}

//	BOOST_CONSTANT(size_t, risk_data_stride = NodeLayout::data_stride);

	//@}

	size_t find_i(key_param_pass_t key) const {
		const HashTp h = HashTp(HashEqual::hash(key));
		const size_t i = h % nBucket;
		for (LinkTp p = bucket[i]; tail != p; p = m_nl.link(p)) {
			TERARK_ASSERT_LT(p, nElem);
			if (HashEqual::equal(key, MyKeyExtractor(m_nl.data(p))))
				return p;
		}
		return nElem; // not found
	}

	template<class CompatibleKey>
	size_t find_i(const CompatibleKey& key) const {
		const HashTp h = HashTp(HashEqual::hash(key));
		const size_t i = h % nBucket;
		for (LinkTp p = bucket[i]; tail != p; p = m_nl.link(p)) {
			TERARK_ASSERT_LT(p, nElem);
			if (HashEqual::equal(key, MyKeyExtractor(m_nl.data(p))))
				return p;
		}
		return nElem; // not found
	}

	// return erased element count
	size_t erase(key_param_pass_t key) {
		const HashTp h = HashTp(HashEqual::hash(key));
		const HashTp i = h % nBucket;
		LinkTp curv, *curp = &bucket[i];
		while (tail != (curv = *curp)) {
			TERARK_ASSERT_LT(curv, nElem);
			TERARK_ASSERT_F(!is_deleted(curv), "%zd", size_t(curv));
			if (HashEqual::equal(key, MyKeyExtractor(m_nl.data(curv)))) {
				*curp = m_nl.link(curv); // delete the curv'th node from collision list
				fast_slot_free(curv);
				return 1;
			}
			curp = &m_nl.link(curv);
		}
		return 0;
	}

	// erase and get the erasing element by get_val
	template<class GetValue>
	size_t erase(key_param_pass_t key, GetValue get_val) {
		const HashTp h = HashTp(HashEqual::hash(key));
		const HashTp i = h % nBucket;
		LinkTp curv, *curp = &bucket[i];
		while (tail != (curv = *curp)) {
			TERARK_ASSERT_LT(curv, nElem);
			TERARK_ASSERT_F(!is_deleted(curv), "%zd", size_t(curv));
			if (HashEqual::equal(key, MyKeyExtractor(m_nl.data(curv)))) {
				get_val(std::move(m_nl.data(curv)));
				*curp = m_nl.link(curv); // delete the curv'th node from collision list
				fast_slot_free(curv);
				return 1;
			}
			curp = &m_nl.link(curv);
		}
		return 0;
	}

	size_t count(key_param_pass_t key) const {
		return find_i(key) == nElem ? 0 : 1;
	}
	// return value is same with count(key), but type is different
	// this function is not stl standard, but more meaningful for app
	bool exists(key_param_pass_t key) const {
		return find_i(key) != nElem;
	}

private:
	// if return non-zero, all permanent id/index are invalidated
	size_t revoke_deleted_no_relink() {
		TERARK_VERIFY_GT(freelist_size, 0);
		NodeLayout nl = m_nl;
		size_t i = 0, n = nElem;
		for (; i < n; ++i)
			if (delmark == nl.link(i))
				goto DoErase;
		TERARK_DIE("it should not go here");
	DoErase:
		HashTp* ph = pHash;
		if (intptr_t(ph) == hash_cache_disabled) {
			for (size_t j = i + 1; j < n; ++j)
				if (delmark != nl.link(j))
					CopyStrategy::move_cons(&nl.data(i), nl.data(j)), ++i;
		} else {
			for (size_t j = i + 1; j < n; ++j)
				if (delmark != nl.link(j)) {
					ph[i] = ph[j]; // copy cached hash value
					CopyStrategy::move_cons(&nl.data(i), nl.data(j)), ++i;
				}
		}
		nElem = LinkTp(i);
		freelist_head = tail;
		freelist_size = 0;
		return n - i; // erased elements count
	}

	// use erased elem as free list link
	HSM_FORCE_INLINE void fast_slot_free(size_t slot) {
		m_nl.link(slot) = delmark;
		m_nl.data(slot).~Elem();
		reinterpret_cast<LinkTp&>(m_nl.data(slot)) = freelist_head;
		freelist_size++;
        freelist_freq++; // never decrease
		freelist_head = LinkTp(slot);
		if (terark_unlikely(m_enable_auto_gc && freelist_size > nElem/2)) {
			revoke_deleted();
		}
	}

public:
	// unlink from bucket and collision list, but keep the elem valid,
	// and delmark is not set, users should call risk_slot_free(slot) later.
	void risk_unlink(size_t idx) {
		const HashTp h = hash_i(idx);
		const size_t bucket_idx = h % nBucket;
		TERARK_ASSERT_GE(nElem, 1);
		TERARK_ASSERT_LT(idx, nElem);
		TERARK_ASSERT_LT(bucket[bucket_idx], nElem);
		TERARK_ASSERT_F(!is_deleted(idx), "idx = %zd", idx);
		LinkTp* curp = &bucket[bucket_idx];
        LinkTp  curr;
		while (idx != (curr = *curp)) {
			curp = &m_nl.link(curr);
			TERARK_ASSERT_LT(*curp, nElem);
		}
		*curp = m_nl.link(idx); // delete the idx'th node from collision list
	}

	void erase_i(const size_t idx) {
		TERARK_ASSERT_GE(nElem, 1);
		TERARK_ASSERT_LT(idx, nElem);
		TERARK_ASSERT_NE(delmark, m_nl.link(idx));
		risk_unlink(idx);
		fast_slot_free(idx);
	}

	const Key& key(size_t idx) const {
		TERARK_ASSERT_GE(nElem, 1);
		TERARK_ASSERT_LT(idx, nElem);
		TERARK_ASSERT_NE(delmark, m_nl.link(idx));
		return MyKeyExtractor(m_nl.data(idx));
	}

	const Key& end_key(size_t idxEnd) const {
		TERARK_ASSERT_GE(nElem, 1);
		TERARK_ASSERT_GE(idxEnd, 1);
		TERARK_ASSERT_LE(idxEnd, nElem);
		TERARK_ASSERT_NE(delmark, m_nl.link(nElem - idxEnd));
		return MyKeyExtractor(m_nl.data(nElem - idxEnd));
	}

	const Elem& elem_at(size_t idx) const {
		TERARK_ASSERT_GE(nElem, 1);
		TERARK_ASSERT_LT(idx, nElem);
		TERARK_ASSERT_NE(delmark, m_nl.link(idx));
		return m_nl.data(idx);
	}
	Elem& elem_at(size_t idx) {
		TERARK_ASSERT_GE(nElem, 1);
		TERARK_ASSERT_LT(idx, nElem);
		TERARK_ASSERT_NE(delmark, m_nl.link(idx));
		return m_nl.data(idx);
	}

	const Elem& end_elem(size_t idxEnd) const {
		TERARK_ASSERT_GE(nElem, 1);
		TERARK_ASSERT_GE(idxEnd, 1);
		TERARK_ASSERT_LE(idxEnd, nElem);
		TERARK_ASSERT_NE(delmark, m_nl.link(nElem - idxEnd));
		return m_nl.data(nElem - idxEnd);
	}
	Elem& end_elem(size_t idxEnd) {
		TERARK_ASSERT_GE(nElem, 1);
		TERARK_ASSERT_GE(idxEnd, 1);
		TERARK_ASSERT_LE(idxEnd, nElem);
		TERARK_ASSERT_NE(delmark, m_nl.link(nElem - idxEnd));
		return m_nl.data(nElem - idxEnd);
	}

	template<class OP>
	void for_each(OP op) {
		size_t n = nElem;
		NodeLayout nl = m_nl;
		if (freelist_size) {
			for (size_t i = 0; i != n; ++i)
				if (delmark != nl.link(i))
					op(nl.data(i));
		} else {
			for (size_t i = 0; i != n; ++i)
				op(nl.data(i));
		}
	}
	template<class OP>
	void for_each(OP op) const {
		const size_t n = nElem;
		const NodeLayout nl = m_nl;
		if (freelist_size) {
			for (size_t i = 0; i != n; ++i)
				if (delmark != nl.link(i))
					op(nl.data(i));
		} else {
			for (size_t i = 0; i != n; ++i)
				op(nl.data(i));
		}
	}

	template<class Compare>	void sort(Compare comp) {
		if (freelist_size)
			revoke_deleted_no_relink();
		fast_iterator beg_iter = m_nl.begin();
		fast_iterator end_iter = beg_iter + nElem;
		terark_parallel_sort(beg_iter, end_iter, Compare(comp));
		relink_fill();
		is_sorted = true;
	}
	void sort() { sort(std::less<Elem>()); }

	size_t bucket_size() const { return nBucket; }

	template<class IntVec>
	void bucket_histogram(IntVec& hist) const {
		for (size_t i = 0, n = nBucket; i < n; ++i) {
			size_t listlen = 0;
			for (LinkTp j = bucket[i]; j != tail; j = m_nl.link(j))
				++listlen;
			if (hist.size() <= listlen)
				hist.resize(listlen+1);
			hist[listlen]++;
		}
	}

protected:
// DataIO support
	template<class DataIO> void dio_load_fast(DataIO& dio) {
		unsigned char cacheHash;
		clear();
		typename DataIO::my_var_uint64_t size;
		dio >> size;
		dio >> load_factor;
		dio >> cacheHash;
		pHash = cacheHash ? NULL : (size_t*)(hash_cache_disabled);
		if (0 == size.t)
			return;
		if (NULL == pHash) {
			pHash = (HashTp*)malloc(sizeof(HashTp)*size.t);
			if (NULL == pHash)
				TERARK_DIE("realloc(%zd)", sizeof(HashTp)*size.t);
		}
		m_nl.reserve(0, size.t);
		size_t i = 0, n = size.t;
		try {
			NodeLayout nl = m_nl;
			for (; i < n; ++i) {
				new(&nl.data(i))Elem();
				dio >> nl.data(i);
			}
			if ((size_t*)(hash_cache_disabled) != pHash) {
				TERARK_VERIFY(nullptr != pHash);
				for (size_t j = 0; j < n; ++j)
					pHash[j] = HashEqual::hash(MyKeyExtractor(nl.data(i)));
			}
			nElem = LinkTp(size.t);
			maxElem = nElem;
			maxload = nElem; //LinkTp(load_factor * nBucket);
			rehash(maxElem / load_factor);
		}
		catch (...) {
			nElem = i; // valid elem count
			clear();
			throw;
		}
	}
	template<class DataIO> void dio_save_fast(DataIO& dio) const {
		dio << typename DataIO::my_var_uint64_t(nElem);
		dio << load_factor;
		dio << (unsigned char)(intptr_t(pHash) != hash_cache_disabled);
		const NodeLayout nl = m_nl;
		for (size_t i = 0, n = nElem; i < n; ++i)
			dio << nl.data(i);
	}

	// compatible format
	template<class DataIO> void dio_load(DataIO& dio) {
		typename DataIO::my_var_uint64_t Size;
		dio >> Size;
		this->clear();
		this->reserve(Size.t);
		for (size_t i = 0, n = Size.t; i < n; ++i) {
			Elem& e = m_nl.data(i); // uninitialized
			new(&e)Elem(); // default cons
			dio >> e;
		}
		this->nElem = LinkTp(Size.t); // set size
		this->relink_fill();
	}
	template<class DataIO> void dio_save(DataIO& dio) const {
		dio << typename DataIO::my_var_uint64_t(this->size());
		if (this->freelist_size) {
			for (size_t i = 0, n = this->end_i(); i < n; ++i)
				if (this->is_deleted(i))
					dio << m_nl.data(i);
		} else {
			for (size_t i = 0, n = this->end_i(); i < n; ++i)
				dio << m_nl.data(i);
		}
	}

	template<class DataIO>
	friend void DataIO_loadObject(DataIO& dio, gold_hash_tab& x) {
		x.dio_load(dio);
	}
	template<class DataIO>
	friend void DataIO_saveObject(DataIO& dio, const gold_hash_tab& x) {
		x.dio_save(dio);
	}

	friend bool operator==(const gold_hash_tab& x, const gold_hash_tab& y) {
		if (x.size() != y.size()) {
			return false;
		}
		const size_t x_end_i = x.end_i();
		const size_t y_end_i = y.end_i();
		for (size_t i = 0; i < x_end_i; ++i) {
			if (!x.is_deleted(i)) {
				const Elem& ex = x.m_nl.data(i);
				const size_t j = y.find_i(x.getKeyExtractor(ex));
				if (j == y_end_i)
					return false;
				if (std::is_same<Key, Elem>::value) {
					// if Key and Elem is same type, we need not to compare
					// elem, just do nothing!
				} else {
					// here has extra compare with key, because we just need
					// to compare non-key part of elem.
					//
					// use operator== for elem compare
					if (!(ex == y.m_nl.data(j)))
						return false;
				}
			}
		}
		return true;
	}
	friend bool operator!=(const gold_hash_tab& x, const gold_hash_tab& y) {
		return !(x == y);
	}
#undef MyKeyExtractor
};

template<class First>
struct terark_get_first {
	template<class T>
	const First& operator()(const T& x) const { return x.first; }
};

template< class Key
		, class Value
		, class HashFunc = DEFAULT_HASH_FUNC<Key>
		, class KeyEqual = std::equal_to<Key>
		, class NodeLayout = node_layout<std::pair<Key, Value>, unsigned>
		, class HashTp = size_t
		>
class gold_hash_map : public
	gold_hash_tab<Key, std::pair<Key, Value>
		, hash_and_equal<Key, HashFunc, KeyEqual>, terark_get_first<Key>
		, NodeLayout
		, HashTp
		>
{
	typedef
	gold_hash_tab<Key, std::pair<Key, Value>
		, hash_and_equal<Key, HashFunc, KeyEqual>, terark_get_first<Key>
		, NodeLayout
		, HashTp
		>
	super;
public:
	typedef typename super::key_param_pass_t key_param_pass_t;
#if 0
	template<class ConsValue>
	struct ConsHelper {
		Key first;
		unsigned char second[sizeof(Value)]; // value memory
		ConsHelper(key_param_pass_t k, ConsValue cons) : first(k) {
			cons(second);
		}
	};
#endif
//	typedef std::pair<const Key, Value> value_type;
	typedef Value mapped_type;
	using super::insert;
	using super::insert_i;
	std::pair<size_t, bool>
	insert_i(key_param_pass_t key, const Value& val) {
		return this->insert_i(std::make_pair(key, val));
	}
	std::pair<size_t, bool>
	insert_i(key_param_pass_t key) {
		return this->lazy_insert_i(key, &default_cons<Value>);
	}
	template<class ConsValue>
	std::pair<size_t, bool>
	lazy_insert_i(key_param_pass_t key, const ConsValue& cons) {
		return this->lazy_insert_elem_i(key, [&](std::pair<Key, Value>* kv_mem) {
		  #if 0
			new(kv_mem) ConsHelper<ConsValue>(key, cons_value);
		  #else
			new(&kv_mem->first) Key(key);
			cons(&kv_mem->second); // if cons fail, key will be leaked
			// but lazy_insert_elem_i have no basic guarantee on cons fail,
			// use ConsHelper have no help to exception safe, so we do not
			// use ConsHelper for simplicity.
		  #endif
		});
	}
	template<class ConsValue, class PreInsert>
	std::pair<size_t, bool>
	lazy_insert_i(key_param_pass_t key, const ConsValue& cons,
				  PreInsert pre_insert) {
		return this->lazy_insert_elem_i(key, [&](std::pair<Key, Value>* kv_mem) {
		  #if 0
			new(kv_mem) ConsHelper<ConsValue>(key, cons_value);
		  #else
			new(&kv_mem->first) Key(key);
			cons(&kv_mem->second); // if cons fail, key will be leaked
			// but lazy_insert_elem_i have no basic guarantee on cons fail,
			// use ConsHelper have no help to exception safe, so we do not
			// use ConsHelper for simplicity.
		  #endif
		}, pre_insert); // copy-paste except this line
	}
	Value& operator[](key_param_pass_t key) {
		std::pair<size_t, bool> ib = this->insert_i(key);
		return this->m_nl.data(ib.first).second;
	}
	Value& at(key_param_pass_t key) {
		size_t idx = this->find_i(key);
		if (this->end_i() != idx)
			return this->m_nl.data(idx).second;
		else
			throw std::out_of_range(BOOST_CURRENT_FUNCTION);
	}
	const Value& at(key_param_pass_t key) const {
		size_t idx = this->find_i(key);
		if (this->end_i() != idx)
			return this->m_nl.data(idx).second;
		else
			throw std::out_of_range(BOOST_CURRENT_FUNCTION);
	}
	Value& val(size_t idx) {
		TERARK_ASSERT_LT(idx, this->nElem);
		TERARK_ASSERT_NE(this->delmark, this->m_nl.link(idx));
		return this->m_nl.data(idx).second;
	}
	const Value& val(size_t idx) const {
		TERARK_ASSERT_LT(idx, this->nElem);
		TERARK_ASSERT_NE(this->delmark, this->m_nl.link(idx));
		return this->m_nl.data(idx).second;
	}
	const Value& end_val(size_t idxEnd) const {
		TERARK_ASSERT_GE(this->nElem, 1);
		TERARK_ASSERT_GE(idxEnd, 1);
		TERARK_ASSERT_LE(idxEnd, this->nElem);
		TERARK_ASSERT_NE(this->delmark, this->m_nl.link(this->nElem - idxEnd));
		return this->m_nl.data(this->nElem - idxEnd).second;
	}
	Value& end_val(size_t idxEnd) {
		TERARK_ASSERT_GE(this->nElem, 1);
		TERARK_ASSERT_GE(idxEnd, 1);
		TERARK_ASSERT_LE(idxEnd, this->nElem);
		TERARK_ASSERT_NE(this->delmark, this->m_nl.link(this->nElem - idxEnd));
		return this->m_nl.data(this->nElem - idxEnd).second;
	}

	using super::erase;
	size_t erase(key_param_pass_t key, Value* erased) {
		return super::erase(key, [erased](std::pair<Key, Value>&& kv) {
			*erased = std::move(kv.second);
		});
	}

// DataIO support
	template<class DataIO>
	friend void DataIO_loadObject(DataIO& dio, gold_hash_map& x) {
		x.dio_load(dio);
	}
	template<class DataIO>
	friend void DataIO_saveObject(DataIO& dio, const gold_hash_map& x) {
		x.dio_save(dio);
	}
	friend bool operator==(const gold_hash_map& x, const gold_hash_map& y) {
		if (x.size() != y.size()) {
			return false;
		}
		const size_t x_end_i = x.end_i();
		const size_t y_end_i = y.end_i();
		for (size_t i = 0; i < x_end_i; ++i) {
			if (!x.is_deleted(i)) {
				const std::pair<Key, Value>& xkv = x.m_nl.data(i);
				const size_t j = y.find_i(xkv.first);
				if (j == y_end_i)
					return false;
				// use operator== for value compare
				if (!(xkv.second == y.m_nl.data(j).second))
					return false;
			}
		}
		return true;
	}
	friend bool operator!=(const gold_hash_map& x, const gold_hash_map& y) {
	    return !(x == y);
    }
};

template< class Key
		, class Value
		, class HashFunc = DEFAULT_HASH_FUNC<Key>
		, class KeyEqual = std::equal_to<Key>
		, class Deleter = HSM_DefaultDeleter
		>
class gold_hash_map_p : public terark_ptr_hash_map
    < gold_hash_map<Key, Value*, HashFunc, KeyEqual>, Value*, Deleter>
{};

///////////////////////////////////////////////////////////////////////////////

template< class Key
		, class HashFunc = DEFAULT_HASH_FUNC<Key>
		, class KeyEqual = std::equal_to<Key>
		, class NodeLayout = node_layout<Key, unsigned>
		, class HashTp = size_t
		>
class gold_hash_set : public
	gold_hash_tab<Key, Key
		, hash_and_equal<Key, HashFunc, KeyEqual>, terark_identity<Key>
		, NodeLayout, HashTp
		>
{
public:
// DataIO support
	template<class DataIO>
	friend void DataIO_loadObject(DataIO& dio, gold_hash_set& x) {
		x.dio_load(dio);
	}
	template<class DataIO>
	friend void DataIO_saveObject(DataIO& dio, const gold_hash_set& x) {
		x.dio_save(dio);
	}
};

} // namespace terark

namespace std { // for std::swap

template< class Key
		, class Elem
		, class HashEqual
		, class KeyExtractor
		, class NodeLayout
		, class HashTp
		>
void
swap(terark::gold_hash_tab<Key, Elem, HashEqual, KeyExtractor, NodeLayout, HashTp>& x,
	 terark::gold_hash_tab<Key, Elem, HashEqual, KeyExtractor, NodeLayout, HashTp>& y)
{
	x.swap(y);
}

template< class Key
		, class Value
		, class HashFunc
		, class KeyEqual
		, class NodeLayout
		, class HashTp
		>
void
swap(terark::gold_hash_map<Key, Value, HashFunc, KeyEqual, NodeLayout, HashTp>& x,
	 terark::gold_hash_map<Key, Value, HashFunc, KeyEqual, NodeLayout, HashTp>& y)
{
	x.swap(y);
}

template< class Key
		, class HashFunc
		, class KeyEqual
		, class NodeLayout
		, class HashTp
		>
void
swap(terark::gold_hash_set<Key, HashFunc, KeyEqual, NodeLayout, HashTp>& x,
	 terark::gold_hash_set<Key, HashFunc, KeyEqual, NodeLayout, HashTp>& y)
{
	x.swap(y);
}

} // namespace std

#if defined(__GNUC__) && __GNUC_MINOR__ + 1000 * __GNUC__ > 7000
  #pragma GCC diagnostic pop
#endif
