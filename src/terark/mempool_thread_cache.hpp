#pragma once
#include <terark/util/function.hpp>
#include <terark/thread/instance_tls_owner.hpp>
#include <boost/integer/static_log2.hpp>
#include <boost/mpl/if.hpp>

namespace terark {

template<int AlignSize> class ThreadCacheMemPool; // forward declare
template<int AlignSize>
class TERARK_DLL_EXPORT TCMemPoolOneThread : boost::noncopyable {
public:
    BOOST_STATIC_ASSERT((AlignSize & (AlignSize-1)) == 0);
    BOOST_STATIC_ASSERT(AlignSize >= 4);
    typedef typename boost::mpl::if_c<AlignSize == 4, uint32_t, uint64_t>::type link_size_t;

    static const size_t skip_list_level_max = 8;    // data io depend on this, don't modify this value
    static const size_t list_tail = ~link_size_t(0);
    static const size_t offset_shift = boost::static_log2<AlignSize>::value;

    typedef link_size_t link_t;
    struct huge_link_t {
        link_size_t size;
        link_size_t next[skip_list_level_max];
    };
    struct head_t {
        head_t() : head(list_tail), cnt(0) {}
        link_size_t head;
        link_size_t cnt;
    };
    size_t         fragment_size;
    intptr_t       m_frag_inc;
    huge_link_t    huge_list; // huge_list.size is max height of skiplist
    valvec<head_t> m_freelist_head;
    size_t  huge_size_sum;
    size_t  huge_node_cnt;
    ThreadCacheMemPool<AlignSize>* m_mempool;
    TCMemPoolOneThread* m_next_free;
    size_t  m_hot_end; // only be accessed by tls
    size_t  m_hot_pos; // only be accessed by tls, frequently changing

    explicit TCMemPoolOneThread(ThreadCacheMemPool<AlignSize>* mp);
    virtual ~TCMemPoolOneThread();

    ThreadCacheMemPool<AlignSize>* tls_owner() const { return m_mempool; }

  #if defined(__GNUC__)
    unsigned int m_rand_seed = 1;
    unsigned int rand() { return rand_r(&m_rand_seed); }
  #endif
    size_t random_level();

    void reduce_frag_size(size_t request);
    size_t alloc(byte_t* base, size_t request);
    size_t alloc3(byte_t* base, size_t oldpos, size_t oldlen, size_t newlen);

    void sfree(byte_t* base, size_t pos, size_t len);
    void set_hot_area(byte_t* base, size_t pos, size_t len);
    void populate_hot_area(byte_t* base, size_t pageSize);

    virtual void clean_for_reuse();
    virtual void init_for_reuse();
};

/// mempool which alloc mem block identified by
/// integer offset(relative address), not pointers(absolute address)
/// integer offset could be 32bit even in 64bit hardware.
///
/// the returned offset is aligned to AlignSize, this allows 32bit
/// integer could address up to 4G*AlignSize memory
///
/// when memory exhausted, valvec can realloc memory without memcpy
/// @see valvec
class TERARK_DLL_EXPORT ThreadCacheMemPoolBase : public valvec<byte_t>, boost::noncopyable {
protected:
    size_t  fragment_size; // for compatible with MemPool_Lock(Free|None|Mutex)
};
template<int AlignSize>
class TERARK_DLL_EXPORT ThreadCacheMemPool : protected ThreadCacheMemPoolBase,
       protected instance_tls_owner<ThreadCacheMemPool<AlignSize>,
                                    TCMemPoolOneThread<AlignSize> >
{
    using TLS =  instance_tls_owner<ThreadCacheMemPool<AlignSize>,
                                    TCMemPoolOneThread<AlignSize> >;
    friend class instance_tls_owner<ThreadCacheMemPool<AlignSize>,
                                    TCMemPoolOneThread<AlignSize> >;
    friend class TCMemPoolOneThread<AlignSize>;
    void clean_for_reuse(TCMemPoolOneThread<AlignSize>* t);
    void init_for_reuse(TCMemPoolOneThread<AlignSize>* t) const;

    ThreadCacheMemPool(const ThreadCacheMemPool&) = delete;
    ThreadCacheMemPool(ThreadCacheMemPool&&) = delete;
    ThreadCacheMemPool& operator=(const ThreadCacheMemPool&) = delete;
    ThreadCacheMemPool& operator=(ThreadCacheMemPool&&) = delete;

    friend class TCMemPoolOneThread<AlignSize>;
    static constexpr size_t ArenaSize = 2 * 1024 * 1024;

    typedef typename TCMemPoolOneThread<AlignSize>::link_t link_t;

protected:

    typedef valvec<unsigned char> mem;
    size_t        m_fastbin_max_size;
    size_t        m_chunk_size = ArenaSize;

    size_t alloc_slow_path(size_t request, TCMemPoolOneThread<AlignSize>*);

    TCMemPoolOneThread<AlignSize>* create_tls_obj() const; // for compile

public:
    using mem::data;
    using mem::size; // bring to public...
    using mem::capacity;
    using mem::risk_set_data;
    using mem::risk_set_capacity;
    using mem::risk_release_ownership;

    using TLS::for_each_tls;
    using TLS::peek_tls_vec_size;

    size_t frag_size() const { return fragment_size; }

    valvec<unsigned char>* get_valvec() { return this; }

    std::function<TCMemPoolOneThread<AlignSize>*(ThreadCacheMemPool*)> m_new_tc;

    bool m_vm_explicit_commit = false;
    size_t m_vm_commit_fail_cnt = 0;
    size_t m_vm_commit_fail_len = 0;

    void set_chunk_size(size_t sz) {
        TERARK_VERIFY_F((sz & (sz-1)) == 0, "%zd(%#zX)", sz, sz);
        m_chunk_size = sz;
    }
    size_t get_chunk_size() const { return m_chunk_size; }

          mem& get_data_byte_vec()       { return *this; }
    const mem& get_data_byte_vec() const { return *this; }

    enum { align_size = AlignSize };

    explicit ThreadCacheMemPool(size_t fastbin_max_size);
    ~ThreadCacheMemPool();

    void sync_frag_size();

    // when calling this function, there should not be any other concurrent
    // thread accessing this mempool's meta data.
    // after this function call, this->fragment_size includs hot area free size
    void sync_frag_size_full();

    // the frag_size including hot area of each thread cache and
    // fragments in freelists
    size_t slow_get_free_size() const;
    size_t get_cur_tls_free_size() const;

    void destroy_and_clean();
    void get_fastbin(valvec<size_t>* fast) const;
    void print_stat(FILE* fp) const;

    size_t get_huge_stat(size_t* huge_memsize) const;

    void risk_set_data(const void* data, size_t len);

    unsigned char byte_at(size_t pos) const {
        assert(pos < mem::n);
        return mem::p[pos];
    }

    void clear();
    void erase_all();
    void reserve(size_t cap);
    void shrink_to_fit();

    template<class U> const U& at(size_t pos) const {
        assert(pos < mem::n);
    //  assert(pos + sizeof(U) < mem::n);
        return *(U*)(mem::p + pos);
    }
    template<class U> U& at(size_t pos) {
        assert(pos < mem::n);
    //  assert(pos + sizeof(U) < mem::n);
        return *(U*)(mem::p + pos);
    }

    bool chunk_alloc(TCMemPoolOneThread<AlignSize>* tc, size_t request);

    static TCMemPoolOneThread<AlignSize>* default_new_tc(ThreadCacheMemPool*);

    TCMemPoolOneThread<AlignSize>* tls() {
        return this->get_tls([this] { return m_new_tc(this); });
    }

    // param request must be aligned by AlignSize
    size_t alloc(size_t request);

    terark_forceinline
    size_t alloc(size_t request, TCMemPoolOneThread<AlignSize>* tc) {
        assert(request > 0);
        assert(nullptr != tc);
        if (AlignSize < sizeof(link_t)) { // const expression
            request = std::max(sizeof(link_t), request);
        }
        request = pow2_align_up(request, AlignSize);
        size_t res = tc->alloc(mem::p, request);
        if (terark_likely(size_t(-1) != res))
            return res;
        else
            return alloc_slow_path(request, tc);
    }

    size_t alloc3(size_t oldpos, size_t oldlen, size_t newlen);
    size_t alloc3(size_t oldpos, size_t oldlen, size_t newlen,
                  TCMemPoolOneThread<AlignSize>* tc);

    void sfree(size_t pos, size_t len);

    terark_forceinline
    void sfree(size_t pos, size_t len, TCMemPoolOneThread<AlignSize>* tc) {
        assert(len > 0);
        assert(pos < mem::n);
        assert(pos % AlignSize == 0);
        if (AlignSize < sizeof(link_t)) { // const expression
            len = std::max(sizeof(link_t), len);
        }
        len = pow2_align_up(len, AlignSize);
        assert(pos + len <= mem::n);
        tc->sfree(mem::p, pos, len);
    }

    void tc_populate(size_t sz);
};

} // namespace terark

