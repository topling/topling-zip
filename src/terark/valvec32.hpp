#pragma once
// generated from valvec.hpp by sed-gen-valvec32.bash
#include "valvec.hpp"
#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wshorten-64-to-32"
#elif defined(__GNUC__) && __GNUC_MINOR__ + 1000 * __GNUC__ > 7000
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wclass-memaccess" // which version support?
#endif

namespace terark {

/// DONT DELETE OR CHANGE THIS LINE

/// similary with std::vector, but:
///   1. use realloc to enlarge/shrink memory, this has avoid memcpy when
///      realloc is implemented by mremap for large chunk of memory;
///      mainstream realloc are implemented in this way
///   2. valvec32 also avoid calling copy-cons/move-cons when enlarge the valvec32
///@Note
///  1. T must be memmove-able, std::list,map,set... are not memmove-able,
///     some std::string with short string optimization is not memmove-able,
///     a such known std::string is g++-5.3's implementation
///  2. std::vector, ... are memmove-able, they could be the T, I'm not 100% sure,
///     since the g++-5.3's std::string has trapped me once
template<class T>
class valvec32 {
protected:
    struct AutoMemory { // for exception-safe
        T* p;
        explicit AutoMemory(size_t n) {
            p = (T*)malloc(sizeof(T) * n);
            if (NULL == p) TERARK_DIE("malloc(%zd)", sizeof(T) * n);
        }
        ~AutoMemory() { if (p) free(p); }
    };

    T*     p;
    uint32_t n;
    uint32_t c; // capacity

    template<class InputIter>
    void construct(InputIter first, ptrdiff_t count) {
        assert(count >= 0);
        p = NULL;
        n = c = 0;
        if (count) { // for exception-safe
            AutoMemory tmp(count);
            std::uninitialized_copy_n(first, count, tmp.p);
            p = tmp.p;
            n = c = count;
            tmp.p = NULL;
        }
    }

    template<class> struct void_ { typedef void type; };

    template<class InputIter>
    void construct(InputIter first, InputIter last, std::input_iterator_tag) {
        p = NULL;
        n = c = 0;
        for (; first != last; ++first)
            this->push_back(*first);
    }
    template<class ForwardIter>
    void construct(ForwardIter first, ForwardIter last, std::forward_iterator_tag) {
        ptrdiff_t count = std::distance(first, last);
        assert(count >= 0);
        construct(first, count);
    }

    template<class InputIter>
    void assign_aux(InputIter first, InputIter last, std::input_iterator_tag) {
        resize(0);
        for (; first != last; ++first)
            this->push_back(*first);
    }
    template<class ForwardIter>
    void assign_aux(ForwardIter first, ForwardIter last, std::forward_iterator_tag) {
        ptrdiff_t count = std::distance(first, last);
        assign(first, count);
    }

public:
    typedef T  value_type;
    typedef T* iterator;
    typedef T& reference;
    typedef const T* const_iterator;
    typedef const T& const_reference;
    typedef typename ParamPassType<T>::type param_type;

    typedef std::reverse_iterator<T*> reverse_iterator;
    typedef std::reverse_iterator<const T*> const_reverse_iterator;

    typedef size_t size_type;
    typedef ptrdiff_t difference_type;

    T* begin() { return p; }
    T* end()   { return p + n; }

    const T* begin() const { return p; }
    const T* end()   const { return p + n; }

    reverse_iterator rbegin() { return reverse_iterator(p + n); }
    reverse_iterator rend()   { return reverse_iterator(p); }

    const_reverse_iterator rbegin() const { return const_reverse_iterator(p + n); }
    const_reverse_iterator rend()   const { return const_reverse_iterator(p); }

    const T* cbegin() const { return p; }
    const T* cend()   const { return p + n; }

    const_reverse_iterator crbegin() const { return const_reverse_iterator(p + n); }
    const_reverse_iterator crend()   const { return const_reverse_iterator(p); }

    valvec32() {
        p = NULL;
        n = c = 0;
    }

    valvec32(size_t sz, param_type val) {
        if (sz) {
            AutoMemory tmp(sz);
            std::uninitialized_fill_n(tmp.p, sz, val);
            p = tmp.p;
            n = c = sz;
            tmp.p = NULL;
        } else {
            p = NULL;
            n = c = 0;
        }
    }
    explicit valvec32(size_t sz) {
        if (sz) {
            AutoMemory tmp(sz);
            always_uninitialized_default_construct_n(tmp.p, sz);
            p = tmp.p;
            n = c = sz;
            tmp.p = NULL;
        } else {
            p = NULL;
            n = c = 0;
        }
    }

    // size = capacity = sz, memory is not initialized/constructed
    terark_no_inline
    valvec32(size_t sz, valvec_no_init) {
        p = NULL;
        n = c = 0;
        if (sz) {
            p = (T*)malloc(sizeof(T) * sz);
            if (NULL == p) {
                TERARK_DIE("malloc(%zd)", sizeof(T) * sz);
            }
            n = c = sz;
        }
    }
    // size = 0, capacity = sz
    terark_no_inline
    valvec32(size_t sz, valvec_reserve) {
        p = NULL;
        n = c = 0;
        if (sz) {
            p = (T*)malloc(sizeof(T) * sz);
            if (NULL == p) {
                TERARK_DIE("realloc(%zd)", sizeof(T) * sz);
            }
            c = sz;
        }
    }

    template<class AnyIter>
    explicit
    valvec32(const std::pair<AnyIter, AnyIter>& rng
         , typename boost::enable_if<is_iterator<AnyIter>,int>::type = 1) {
        construct(rng.first, rng.second, typename std::iterator_traits<AnyIter>::iterator_category());
    }
    explicit
    valvec32(const std::pair<const T*, const T*>& rng) {
        construct(rng.first, rng.second - rng.first);
    }
    valvec32(const T* first, const T* last) { construct(first, last-first); }
    valvec32(const T* first, ptrdiff_t len) { construct(first, len); }
    template<class AnyIter>
    valvec32(AnyIter first, AnyIter last
        , typename boost::enable_if<is_iterator<AnyIter>, int>::type = 1) {
        construct(first, last, typename std::iterator_traits<AnyIter>::iterator_category());
    }
    template<class InputIter>
    valvec32(InputIter first, ptrdiff_t count
        , typename boost::enable_if<is_iterator<InputIter>, int>::type = 1) 	{
        assert(count >= 0);
        construct(first, count);
    }

    valvec32(const valvec32& y) {
        assert(this != &y);
        assert(!is_object_overlap(this, &y));
        construct(y.p, y.n);
    }

    valvec32& operator=(const valvec32& y) {
        if (&y == this)
            return *this;
        assert(!is_object_overlap(this, &y));
        assign(y.p, y.n);
        return *this;
    }

    valvec32(valvec32&& y) noexcept {
        assert(this != &y);
        assert(!is_object_overlap(this, &y));
        p = y.p;
        n = y.n;
        c = y.c;
        y.p = NULL;
        y.n = 0;
        y.c = 0;
    }

    valvec32& operator=(valvec32&& y) noexcept {
        assert(this != &y);
        if (&y == this)
            return *this;
        assert(!is_object_overlap(this, &y));
        this->~valvec32();
        new(this)valvec32(std::move(y));
        return *this;
    }

    ~valvec32() {
        if (c) { // minimal works, allow `0==c` but `p!=NULL`
            destroy_and_free();
        }
    }

    void fill(param_type x) {
        std::fill_n(p, n, x);
    }

    void fill(size_t pos, size_t cnt, param_type x) {
        assert(pos <= n);
        assert(pos + cnt <= n);
        std::fill_n(p + pos, cnt, x);
    }

    template<class AnyIter>
    typename boost::enable_if<is_iterator<AnyIter>, void>::type
    assign(const std::pair<AnyIter, AnyIter>& rng) {
        assign_aux(rng.first, rng.second, typename std::iterator_traits<AnyIter>::iterator_category());
    }
    template<class AnyIter>
    typename boost::enable_if<is_iterator<AnyIter>, void>::type
    assign(AnyIter first, AnyIter last) {
        assign_aux(first, last, typename std::iterator_traits<AnyIter>::iterator_category());
    }
    template<class InputIter>
    typename boost::enable_if<is_iterator<InputIter>, void>::type
    assign(InputIter first, ptrdiff_t len) {
        assert(len >= 0);
        erase_all();
        reserve(len);
        std::uninitialized_copy_n(first, len, p);
        n = len;
    }
    void assign(const std::pair<const T*, const T*>& rng) {
        assign(rng.first, rng.second);
    }
    void assign(const T* first, const T* last) {
        assign(first, last - first);
    }
    void assign(const T* first, ptrdiff_t len) {
        assert(len >= 0);
        erase_all();
        reserve(len);
        std::uninitialized_copy_n(first, len, p);
        n = len;
    }
    template<class Container>
    typename void_<typename Container::const_iterator>::type
    assign(const Container& cont) { assign(cont.begin(), cont.size()); }

    void clear() noexcept {
        if (c) { // minimal works, allow `0==c` but `p!=NULL`
            destroy_and_free();
        }
        p = NULL;
        n = c = 0;
    }

    const T* data() const { return p; }
          T* data()       { return p; }

    const T* finish() const { return p + c; }
          T* finish()       { return p + c; }

    bool  empty() const { return 0 == n; }
    bool   full() const { return c == n; }
    size_t size() const { return n; }
    size_t capacity() const { return c; }
    size_t unused() const { return c - n; }

    size_t used_mem_size() const { return sizeof(T) * n; }
    size_t full_mem_size() const { return sizeof(T) * c; }
    size_t free_mem_size() const { return sizeof(T) * (c - n); }

    void reserve(size_t newcap) {
        TERARK_ASSERT_LE(n, c);
        if (newcap <= c)
            return; // nothing to do
        reserve_slow(newcap);
    }

private:
    terark_no_inline
    void reserve_slow(size_t newcap) {
        assert(newcap > c);
        TERARK_VERIFY_LE(n, c);
        T* q = (T*)realloc(p, sizeof(T) * newcap);
        if (NULL == q) TERARK_DIE("realloc(%zd)", sizeof(T) * newcap);
        p = q;
        c = newcap;
    }
    terark_no_inline void destroy_and_free() {
        TERARK_VERIFY(nullptr != p);
        TERARK_VERIFY_LE(n, c);
        STDEXT_destroy_range(p, p + n);
        free(p);
    }

public:
    T* ensure_unused(size_t unused_cap) {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        ensure_capacity(oldsize + unused_cap);
        return p + oldsize;
    }
    void ensure_capacity(size_t min_cap) {
        TERARK_ASSERT_LE(n, c);
        if (terark_likely(min_cap <= c)) {
            // nothing to do
            return;
        }
        ensure_capacity_slow(min_cap);
    }
private:
    terark_no_inline
    void ensure_capacity_slow(size_t min_cap) {
        TERARK_VERIFY_LT(c, min_cap);
        TERARK_VERIFY_LE(n, c);
        size_t new_cap = std::max(larger_capacity(c), min_cap);
#if defined(TERARK_VALVEC_HAS_WEAK_SYMBOL)
        if (xallocx) {
            size_t extra = sizeof(T) * (new_cap - min_cap);
            size_t minsz = sizeof(T) * min_cap;
            size_t usesz = xallocx(p, minsz, extra, 0);
            if (usesz >= minsz) {
                c = usesz / sizeof(T); // done
                return;
            }
        }
        else if (malloc_usable_size) {
            size_t usesz = malloc_usable_size(p);
            if (usesz >= sizeof(T) * min_cap) {
                c = usesz / sizeof(T); // done
                return;
            }
        }
#endif
        T* q = (T*)realloc(p, sizeof(T) * new_cap);
        if (NULL == q) TERARK_DIE("realloc(%zd)", sizeof(T) * new_cap);
        p = q;
        c = new_cap;
    }

public:
    void try_capacity(size_t min_cap, size_t max_cap) {
        TERARK_ASSERT_LE(n, c);
        if (terark_likely(min_cap <= c)) {
            // nothing to do
            return;
        }
        try_capacity_slow(min_cap, max_cap);
    }
private:
    terark_no_inline
    void try_capacity_slow(size_t min_cap, size_t max_cap) {
        TERARK_VERIFY_LT(c, min_cap);
        TERARK_VERIFY_LE(n, c);
        if (max_cap < min_cap) {
            max_cap = min_cap;
        }
        size_t cur_cap = max_cap;
        for (;;) {
            T* q = (T*)realloc(p, sizeof(T) * cur_cap);
            if (q) {
                p = q;
                c = cur_cap;
                return;
            }
            if (cur_cap == min_cap) {
                TERARK_DIE("realloc(%zd)", sizeof(T) * cur_cap);
            }
            cur_cap = min_cap + (cur_cap - min_cap) / 2;
        }
    }

public:
    T& ensure_get(size_t i) {
        if (i < n)
            return p[i];
        else
            return ensure_get_slow(i);
    }
    T& ensure_set(size_t i, param_type x) {
        if (i < n)
            return p[i] = x;
        else
            return ensure_set_slow(i, x);
    }
private:
    terark_no_inline
    T& ensure_get_slow(size_t i) {
        assert(n <= i);
        resize_slow(i+1);
        return p[i];
    }
    terark_no_inline
    T& ensure_set_slow(size_t i, param_type x) {
        assert(n <= i);
        resize_slow(i+1);
        T* beg = p; // load p into register
        beg[i] = x;
        return beg[i];
    }
public:

    terark_no_inline
    void shrink_to_fit() noexcept {
        TERARK_VERIFY_LE(n, c);
        if (n == c)
            return;
        if (n) {
            if (T* q = (T*)realloc(p, sizeof(T) * n)) {
                p = q;
                c = n;
            }
        } else {
            if (p)
                free(p);
            p = NULL;
            c = n = 0;
        }
    }

    // may do nothing
    void shrink_to_fit_inplace() noexcept {
#if defined(TERARK_VALVEC_HAS_WEAK_SYMBOL)
        if (xallocx) {
            size_t usesz = xallocx(p, sizeof(T) * n, 0, 0);
            c = usesz / sizeof(T); // done
        }
#endif
    }

    // expect this function will reduce memory fragment
    terark_no_inline
    void shrink_to_fit_malloc_free() noexcept {
        TERARK_VERIFY_LE(n, c);
        if (0 == c) return;
        assert(NULL != p);
        if (0 == n) {
            free(p);
            p = NULL;
            c = 0;
            return;
        }
        if (n == c)
            return;
        // malloc new and free old even if n==c
        // because this may trigger the memory compaction
        // of the malloc implementation and reduce memory fragment
        if (T* q = (T*)malloc(sizeof(T) * n)) {
            memcpy(q, p, sizeof(T) * n);
            free(p);
            c = n;
            p = q;
        } else {
            shrink_to_fit();
        }
    }

    void resize(size_t newsize, param_type val) {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        if (boost::has_trivial_destructor<T>::value) {
            if (newsize <= oldsize) {
                n = newsize;
                return;
            }
        }
        else {
            if (newsize == oldsize)
                return;
        }
        resize_slow(newsize, val);
    }
private:
    terark_no_inline
    void resize_slow(size_t newsize, param_type val) {
        TERARK_VERIFY_LE(n, c);
        size_t oldsize = n;
        assert(oldsize != newsize);
        if (!boost::has_trivial_destructor<T>::value && newsize <= oldsize) {
            STDEXT_destroy_range(p + newsize, p + oldsize);
        }
        else {
            assert(oldsize < newsize);
            ensure_capacity(newsize);
            std::uninitialized_fill_n(p+oldsize, newsize-oldsize, val);
        }
        n = newsize;
    }

public:
    void resize(size_t newsize) {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        if (boost::has_trivial_destructor<T>::value) {
            if (newsize <= oldsize) {
                n = newsize;
                return;
            }
        }
        else {
            if (newsize == oldsize)
                return;
        }
        resize_slow(newsize);
    }
private:
    terark_no_inline
    void resize_slow(size_t newsize) {
        TERARK_VERIFY_LE(n, c);
        size_t oldsize = n;
        assert(oldsize != newsize);
        if (!boost::has_trivial_destructor<T>::value && newsize <= oldsize) {
            STDEXT_destroy_range(p + newsize, p + oldsize);
        }
        else {
            assert(oldsize < newsize);
            ensure_capacity(newsize);
            always_uninitialized_default_construct_n(p + oldsize, newsize - oldsize);
        }
        n = newsize;
    }

public:
    terark_no_inline
    void resize_fill(size_t newsize) {
        TERARK_ASSERT_LE(n, c);
        ensure_capacity(newsize);
        STDEXT_destroy_range(p, p + n);
        if (!std::is_nothrow_default_constructible<T>::value) {
            n = 0;
        }
        always_uninitialized_default_construct_n(p, newsize);
        n = newsize;
    }

    terark_no_inline
    void resize_fill(size_t newsize, param_type val) {
        TERARK_ASSERT_LE(n, c);
        ensure_capacity(newsize);
        STDEXT_destroy_range(p, p + n);
        if (!std::is_nothrow_copy_constructible<T>::value) {
            n = 0;
        }
        std::uninitialized_fill_n(p, newsize, val);
        n = newsize;
    }

    void erase_all() noexcept {
        if (!boost::has_trivial_destructor<T>::value)
            STDEXT_destroy_range(p, p + n);
        n = 0;
    }

    // client code should pay the risk for performance gain
    void resize_no_init(size_t newsize) {
        TERARK_ASSERT_LE(n, c);
    //  assert(boost::has_trivial_constructor<T>::value);
        ensure_capacity(newsize);
        n = newsize;
    }
    // client code should pay the risk for performance gain
    void resize0() { n = 0; }

    ///  trim [from, end)
    void trim(T* from) {
        assert(from >= p);
        assert(from <= p + n);
        if (!boost::has_trivial_destructor<T>::value)
            STDEXT_destroy_range(from, p + n);
        n = from - p;
    }

    /// trim [from, size)
    /// @param from is the new size
    /// when trim(0) is ambiguous, use vec.erase_all(), or:
    /// vec.trim(size_t(0));
    void trim(size_t from) noexcept {
        assert(from <= n);
        if (!boost::has_trivial_destructor<T>::value)
            STDEXT_destroy_range(p + from, p + n);
        n = from;
    }

    void insert(const T* pos, param_type x) {
        TERARK_ASSERT_LE(n, c);
        assert(pos <= p + n);
        assert(pos >= p);
        insert(pos-p, x);
    }

    terark_no_inline
    void insert(size_t pos, param_type x) {
        TERARK_ASSERT_LE(n, c);
        assert(pos <= n);
        if (pos > n) {
            throw std::out_of_range("valvec32::insert");
        }
        ensure_capacity(n+1);
    //	for (ptrdiff_t i = n; i > pos; --i) memcpy(p+i, p+i-1, sizeof T);
        memmove(p+pos+1, p+pos, sizeof(T)*(n-pos));
        new(p+pos)T(x);
        ++n;
    }

    template<class InputIter>
    terark_no_inline
    void insert(size_t pos, InputIter iter, size_t count) {
        TERARK_ASSERT_LE(n, c);
        assert(pos <= n);
        if (pos > n) {
            throw std::out_of_range("valvec32::insert");
        }
        ensure_capacity(n+count);
    //	for (ptrdiff_t i = n; i > pos; --i) memcpy(p+i, p+i-1, sizeof T);
        memmove(p+pos+count, p+pos, sizeof(T)*(n-pos));
        std::uninitialized_copy_n(iter, count, p+pos);
        n += count;
    }

    void push_back() {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        assert(oldsize <= c);
        if (terark_unlikely(oldsize < c)) {
            new(p + oldsize)T(); // default cons
            n = oldsize + 1;
        } else {
            push_back_slow();
        }
    }
private:
    terark_no_inline
    void push_back_slow() {
        TERARK_VERIFY_LE(n, c);
        size_t oldsize = n;
        ensure_capacity(oldsize + 1);
        new(p + oldsize)T(); // default cons
        n = oldsize + 1;
    }

public:
    void push_back(param_type x) {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        if (terark_likely(oldsize < c)) {
            new(p+oldsize)T(x); // copy cons
            n = oldsize + 1;
        } else {
            push_back_slow(x);
        }
    }
private:
    terark_no_inline
    void push_back_slow(param_type x) {
        TERARK_VERIFY_LE(n, c);
        size_t oldsize = n;
        if (!ParamPassType<T>::is_pass_by_value &&
                &x >= p && &x < p + oldsize) {
            size_t idx = &x - p;
            ensure_capacity(oldsize + 1);
            new(p + oldsize)T(p[idx]); // copy cons
        }
        else {
            ensure_capacity(oldsize + 1);
            new(p + oldsize)T(x); // copy cons
        }
        n = oldsize + 1;
    }

public:
    void append(param_type x) { push_back(x); } // alias for push_back
    template<class Iterator>
    void append(Iterator first, ptrdiff_t len) {
        TERARK_ASSERT_LE(n, c);
        assert(len >= 0);
        size_t newsize = n + len;
        ensure_capacity(newsize);
        std::uninitialized_copy_n(first, len, p+n);
        n = newsize;
    }
    template<class Iterator>
    void append(Iterator first, Iterator last) {
        TERARK_ASSERT_LE(n, c);
        ptrdiff_t len = std::distance(first, last);
        size_t newsize = n + len;
        ensure_capacity(newsize);
        std::uninitialized_copy(first, last, p+n);
        n = newsize;
    }
    template<class Iterator>
    void append(const std::pair<Iterator, Iterator>& rng) {
        TERARK_ASSERT_LE(n, c);
        append(rng.first, rng.second);
    }
    template<class Container>
    typename void_<typename Container::const_iterator>::type
    append(const Container& cont) {
        TERARK_ASSERT_LE(n, c);
        append(cont.begin(), cont.end());
    }
    template<class Iterator>
    void unchecked_append(Iterator first, ptrdiff_t len) {
        TERARK_ASSERT_LE(n, c);
        TERARK_ASSERT_GE(len, 0);
        TERARK_ASSERT_LE(len, ptrdiff_t(c - n));
        size_t newsize = n + len;
        std::uninitialized_copy_n(first, len, p+n);
        n = newsize;
    }

    terark_no_inline
    void push_n(size_t cnt, param_type val) {
        TERARK_ASSERT_LE(n, c);
        if (boost::has_trivial_copy<T>::value) {
            std::uninitialized_fill_n(grow_no_init(cnt), cnt, val);
        }
        else {
            ensure_capacity(n + cnt);
            for (size_t i = 0; i < cnt; ++i) {
                unchecked_push_back(val);
            }
        }
    }

    T* grow_capacity(size_t cnt) {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        ensure_capacity(oldsize + cnt);
        return p + oldsize;
    }
    T* grow_no_init(size_t cnt) {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        resize_no_init(oldsize + cnt);
        return p + oldsize;
    }

    T* grow(size_t cnt, param_type x) {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        resize(oldsize + cnt, x);
        return p + oldsize;
    }

    T* grow(size_t cnt) {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        resize(oldsize + cnt);
        return p + oldsize;
    }

    // T must be memmove-able
    T* make_space_no_init(size_t pos, size_t len) {
        TERARK_ASSERT_LE(n, c);
        TERARK_VERIFY_LE(pos, n);
        size_t movecnt = n - pos;
        resize_no_init(n + len);
        T* space = p + pos;
        memmove(space + len, space, sizeof(T) * movecnt);
        return space;
    }

    void unchecked_push_back() {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        assert(oldsize < c);
        new(p+oldsize)T(); // default cons
        n = oldsize + 1;
    }
    void unchecked_push_back(param_type x) {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        assert(oldsize < c);
        new(p+oldsize)T(x); // copy cons
        n = oldsize + 1;
    }

    void pop_back() noexcept {
        assert(n > 0);
        size_t newsize = n-1;
        p[newsize].~T();
        n = newsize;
    }

// use valvec32 as stack ...
//
    void pop_n(size_t num) noexcept {
        assert(num <= this->n);
        STDEXT_destroy_range(this->p + this->n - num, this->p + this->n);
        this->n -= num;
    }
    void pop() noexcept { pop_back(); }
    void push() { push_back(); } // alias for push_back
    void push(param_type x) { push_back(x); } // alias for push_back

    const T& top() const {
        if (0 == n)
            throw std::logic_error("valvec32::top() const, valec is empty");
        return p[n-1];
    }
    T& top() {
        if (0 == n)
            throw std::logic_error("valvec32::top(), valec is empty");
        return p[n-1];
    }

    T pop_val() noexcept {
        assert(n > 0);
        T x(std::move(p[n-1]));
        --n;
        return x;
    }

    void unchecked_push() { unchecked_push_back(); }
    void unchecked_push(param_type x) { unchecked_push_back(x); }

// end use valvec32 as stack

    const T& operator[](size_t i) const {
        assert(i < n);
        return p[i];
    }

    T& operator[](size_t i) {
        assert(i < n);
        return p[i];
    }

    const T& at(size_t i) const {
        if (i >= n) throw std::out_of_range("valvec32::at");
        return p[i];
    }

    T& at(size_t i) {
        if (i >= n) throw std::out_of_range("valvec32::at");
        return p[i];
    }

    T& ref(size_t i) {
        assert(i < n);
        return p[i];
    }
    const T& ref(size_t i) const {
        assert(i < n);
        return p[i];
    }
    const T& cref(size_t i) const {
        assert(i < n);
        return p[i];
    }
    T* ptr(size_t i) {
        assert(i < n);
        return p + i;
    }
    const T* ptr(size_t i) const {
        assert(i < n);
        return p + i;
    }
    const T* cptr(size_t i) const {
        assert(i < n);
        return p + i;
    }

    void set(size_t i, param_type val) {
        assert(i < n);
        p[i] = val;
    }

    const T& front() const {
        assert(n);
        assert(p);
        return p[0];
    }
    T& front() {
        assert(n);
        assert(p);
        return p[0];
    }

    const T& back() const {
        assert(n);
        assert(p);
        return p[n-1];
    }
    T& back() {
        assert(n);
        assert(p);
        return p[n-1];
    }

    T& ende(size_t d) {
        assert(d <= n);
        return p[n-d];
    }
    const T& ende(size_t d) const {
        assert(d <= n);
        return p[n-d];
    }

    void operator+=(param_type x) {
        TERARK_ASSERT_LE(n, c);
        push_back(x);
    }

    void operator+=(const valvec32& y) {
        TERARK_ASSERT_LE(n, c);
        size_t newsize = n + y.size();
        ensure_capacity(newsize);
        std::uninitialized_copy(y.p, y.p + y.n, p+n);
        n = newsize;
    }

    void swap(valvec32& y) noexcept {
        std::swap(p, y.p);
        std::swap(n, y.n);
        std::swap(c, y.c);
    }

    T& emplace_back() {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        assert(oldsize <= c);
        if (terark_unlikely(oldsize < c)) {
            T* lp = p;
            new(lp + oldsize)T(); // default cons
            n = oldsize + 1;
            return lp[oldsize];
        } else {
            push_back_slow();
            return p[oldsize];
        }
    }

#if defined(_MSC_VER) && _MSC_VER <= 1800
// C++: internal compiler error: variadic perfect forwarding
// https://connect.microsoft.com/VisualStudio/feedback/details/806017/c-internal-compiler-error-variadic-perfect-forwarding-to-base-class
// Can not call std::forward multiple times, even in different branch
// this emplace_back is buggy for vec.emplace_back(vec[0]);
    template<class... Args>
    T& emplace_back(Args&&... args) {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        ensure_capacity(oldsize + 1);
        T* lp = p;
        new(lp+oldsize)T(std::forward<Args>(args)...);
        n = oldsize + 1;
        return lp[oldsize];
    }
#else
    template<class... Args>
    T& emplace_back(Args&&... args) {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        assert(oldsize <= c);
        if (oldsize < c) {
            T* lp = p;
            new(lp+oldsize)T(std::forward<Args>(args)...);
            n = oldsize + 1;
            return lp[oldsize];
        } else {
            return emplace_back_slow(std::forward<Args>(args)...);
        }
    }
private:
    template<class... Args>
    terark_no_inline
    T& emplace_back_slow(Args&&... args) {
        TERARK_VERIFY_LE(n, c);
        assert(n == c);
        T val(std::forward<Args>(args)...);
        ensure_capacity_slow(n+1);
        new(p+n)T(std::move(val));
        return p[n++];
    }
public:
    template<class... Args>
    T& unchecked_emplace_back(Args&&... args) {
        TERARK_ASSERT_LE(n, c);
        size_t oldsize = n;
        assert(oldsize < c);
        T* lp = p;
        new(lp+oldsize)T(std::forward<Args>(args)...);
        n = oldsize + 1;
        return lp[n];
    }
#endif

    explicit valvec32(std::initializer_list<T> list) {
        construct(list.begin(), list.size());
    }

    terark_no_inline
    size_t erase_i(size_t pos, size_t cnt) noexcept {
        assert(cnt <= this->n);
        assert(pos <= this->n);
        assert(pos + cnt <= this->n);
        STDEXT_destroy_range(p + pos, p + pos + cnt);
        memmove(p + pos, p + pos + cnt, sizeof(T) * (n - cnt - pos));
        n -= cnt;
        return pos;
    }

    std::pair<T*, T*> range() { return std::make_pair(p, p+n); }
    std::pair<const T*, const T*> range() const { return std::make_pair(p, p+n); }

    const T& get_2d(size_t colsize, size_t row, size_t col) const {
        size_t idx = row * colsize + col;
        assert(idx < n);
        return p[idx];
    }
    T& get_2d(size_t colsize, size_t row, size_t col) {
        size_t idx = row * colsize + col;
        assert(idx < n);
        return p[idx];
    }

    void risk_set_data(T* Data, size_t Size) {
        p = Data;
        n = Size;
        c = Size;
    }
    void risk_set_data(T* data) { p = data; }
    void risk_set_size(size_t size) { this->n = size; }
    void risk_set_capacity(size_t capa) { this->c = capa; }
    void risk_set_end(T* endp) {
        assert(endp - p <= ptrdiff_t(c));
        assert(endp - p >= 0);
        n = endp - p;
    }

    T* risk_release_ownership() noexcept {
    //	BOOST_STATIC_ASSERT(boost::has_trivial_destructor<T>::value);
        T* q = p;
        p = NULL;
        n = c = 0;
        return q;
    }

    void risk_destroy(MemType mt) {
        switch (mt) {
            default:
                TERARK_DIE("invalid MemType = %d", int(mt));
                break;
            case MemType::Malloc:
                clear();
                break;
            case MemType::Mmap:
                assert(n <= c);
                TERARK_VERIFY(size_t(p) % 4096 == 0);
                mmap_close(p, sizeof(T) * c);
                no_break_fallthrough; // fall through
            case MemType::User:
                risk_release_ownership();
                break;
        }
    }

/*
 * Now serialization valvec32<T> has been builtin DataIO
 *
    template<class DataIO>
    friend void DataIO_saveObject(DataIO& dio, const valvec32& x) {
        typename DataIO::my_var_uint64_t size(x.n);
        dio << size;
        // complex object has not implemented yet!
        BOOST_STATIC_ASSERT(boost::has_trivial_destructor<T>::value);
        dio.ensureWrite(x.p, sizeof(T)*x.n);
    }

       template<class DataIO>
    friend void DataIO_loadObject(DataIO& dio, valvec32& x) {
        typename DataIO::my_var_uint64_t size;
        dio >> size;
        x.resize_no_init(size.t);
        // complex object has not implemented yet!
        BOOST_STATIC_ASSERT(boost::has_trivial_destructor<T>::value);
        dio.ensureRead(x.p, sizeof(T)*x.n);
    }
*/

    template<class TLess, class TEqual>
    static bool lessThan(const valvec32& x, const valvec32& y, TLess le, TEqual eq) {
        size_t n = std::min(x.n, y.n);
        for (size_t i = 0; i < n; ++i) {
            if (!eq(x.p[i], y.p[i]))
                return le(x.p[i], y.p[i]);
        }
        return x.n < y.n;
    }
    template<class TEqual>
    static bool equalTo(const valvec32& x, const valvec32& y, TEqual eq) {
        if (x.n != y.n)
            return false;
        for (size_t i = 0, n = x.n; i < n; ++i) {
            if (!eq(x.p[i], y.p[i]))
                return false;
        }
        return true;
    }
    template<class TLess = std::less<T>, class TEqual = std::equal_to<T> >
    struct less : TLess, TEqual {
        bool operator()(const valvec32& x, const valvec32& y) const {
            return lessThan<TLess, TEqual>(x, y, *this, *this);
        }
        less() {}
        less(TLess le, TEqual eq) : TLess(le), TEqual(eq) {}
    };
    template<class TEqual = std::equal_to<T> >
    struct equal : TEqual {
        bool operator()(const valvec32& x, const valvec32& y) const {
            return equalTo<TEqual>(x, y, *this);
        }
        equal() {}
        equal(TEqual eq) : TEqual(eq) {}
    };
};

template<class T>
bool valvec_lessThan(const valvec32<T>& x, const valvec32<T>& y) {
    return valvec32<T>::lessThan(x, y, std::less<T>(), std::equal_to<T>());
}
template<class T>
bool valvec_equalTo(const valvec32<T>& x, const valvec32<T>& y) {
    return valvec32<T>::equalTo(x, y, std::equal_to<T>());
}

template<class T>
bool operator==(const valvec32<T>& x, const valvec32<T>& y) {
    return valvec32<T>::equalTo(x, y, std::equal_to<T>());
}
template<class T>
bool operator!=(const valvec32<T>& x, const valvec32<T>& y) {
    return !valvec32<T>::equalTo(x, y, std::equal_to<T>());
}
template<class T>
bool operator<(const valvec32<T>& x, const valvec32<T>& y) {
    return valvec32<T>::lessThan(x, y, std::less<T>(), std::equal_to<T>());
}
template<class T>
bool operator>(const valvec32<T>& x, const valvec32<T>& y) {
    return valvec32<T>::lessThan(y, x, std::less<T>(), std::equal_to<T>());
}
template<class T>
bool operator<=(const valvec32<T>& x, const valvec32<T>& y) {
    return !valvec32<T>::lessThan(y, x, std::less<T>(), std::equal_to<T>());
}
template<class T>
bool operator>=(const valvec32<T>& x, const valvec32<T>& y) {
    return !valvec32<T>::lessThan(x, y, std::less<T>(), std::equal_to<T>());
}


} // namespace terark

namespace std {
    template<class T>
    void swap(terark::valvec32<T>& x, terark::valvec32<T>& y) noexcept { x.swap(y); }
}

#if defined(__clang__)
  #pragma clang diagnostic pop
#elif defined(__GNUC__) && __GNUC_MINOR__ + 1000 * __GNUC__ > 7000
  #pragma GCC diagnostic pop
#endif

