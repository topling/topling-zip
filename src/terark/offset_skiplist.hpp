#pragma once

// Created by leipeng on 2026-09-09
//
// Offset-based skip list. Nodes live in ThreadCacheMemPool (same allocator
// as cspptrie). Links are loc = byte_offset / AlignSize, not pointers.
//
// File hierarchy parallel to CSPPMemTable:
//   offset_skiplist.hpp                                 ADT (this file)
//   topling-sst/src/table/offset_skiplist_rep.cc        MemTableRep + SidePlugin

#include <terark/config.hpp>
#include <terark/fstring.hpp>
#include <terark/mempool_thread_cache.hpp>
#include <terark/stdtypes.hpp>
#include <terark/util/atomic.hpp>
#include <terark/util/cpu_prefetch.hpp>
#include <terark/util/mmap.hpp>
#include <terark/util/throw.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef _MSC_VER
#include <cerrno>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace terark {

template<int AlignSize, class Node>
class OffsetSkipListNodeBase {
    static_assert(AlignSize == 4 || AlignSize == 8, "AlignSize must be 4 or 8");

  public:
    using link_t =
        typename std::conditional<AlignSize == 4, uint32_t, uint64_t>::type;
    static const link_t nil = link_t(-1);

    void StashHeight(const int height) {
        TERARK_ASSERT_LE(sizeof(int), sizeof(m_next[0]));
        memcpy(static_cast<void*>(&m_next[0]), &height, sizeof(int));
    }
    int UnstashHeight() const {
        int rv;
        memcpy(&rv, &m_next[0], sizeof(int));
        return rv;
    }
    const char* Key() const {
        return reinterpret_cast<const char*>(static_cast<const Node*>(this) + 1);
    }

    static link_t LocOf(const byte_t* base, const Node* n) {
        TERARK_ASSERT_NE(n, nullptr);
        auto off = reinterpret_cast<const byte_t*>(n) - base;
        TERARK_ASSERT_AL(off, AlignSize);
        return link_t(size_t(off) / AlignSize);
    }
    static Node* NodeAt(byte_t* base, link_t loc) {
        TERARK_ASSERT_NE(loc, nil);
        return reinterpret_cast<Node*>(base + size_t(loc) * AlignSize);
    }

    link_t NextLoc(int n) const {
        TERARK_ASSERT_GE(n, 0);
        return m_next[-n].load(std::memory_order_acquire);
    }
    void SetNextLoc(int n, link_t loc) {
        TERARK_ASSERT_GE(n, 0);
        m_next[-n].store(loc, std::memory_order_release);
    }
    bool CASNextLoc(int n, link_t expected, link_t x) {
        TERARK_ASSERT_GE(n, 0);
        return m_next[-n].compare_exchange_strong(expected, x);
    }
    void NoBarrier_SetNextLoc(int n, link_t loc) {
        TERARK_ASSERT_GE(n, 0);
        m_next[-n].store(loc, std::memory_order_relaxed);
    }

  protected:
    // m_next[0] is level 0. Higher levels sit immediately before the Node.
    std::atomic<link_t> m_next[1];
};

template<int AlignSize, class Value>
class OffsetSkipListNode
    : public OffsetSkipListNodeBase<AlignSize,
                                    OffsetSkipListNode<AlignSize, Value>> {
  public:
    Value m_val;
};

template<int AlignSize>
class OffsetSkipListNode<AlignSize, void>
    : public OffsetSkipListNodeBase<AlignSize,
                                    OffsetSkipListNode<AlignSize, void>> {};

template<class Comparator, int AlignSize = 4, class Value = void>
class OffsetSkipList {
    static_assert(AlignSize == 4 || AlignSize == 8, "AlignSize must be 4 or 8");

  public:
    using DecodedKey =
        typename std::remove_reference<Comparator>::type::DecodedType;
    using link_t =
        typename std::conditional<AlignSize == 4, uint32_t, uint64_t>::type;
    using Node = OffsetSkipListNode<AlignSize, Value>;

    class Token;
  private:
    struct LazyFreeListTLS;
  public:
    using MemTls = LazyFreeListTLS;
    struct Splice {
        int height = 0;
        link_t* prev;
        link_t* next;
    };

  private:
    Splice* AllocateSpliceOnHeap() const {
        size_t array_size = sizeof(link_t) * (m_max_height_limit + 1);
        char* raw = new char[sizeof(Splice) + array_size * 2];
        Splice* splice = reinterpret_cast<Splice*>(raw);
        splice->height = 0;
        splice->prev = reinterpret_cast<link_t*>(raw + sizeof(Splice));
        splice->next = reinterpret_cast<link_t*>(raw + sizeof(Splice) + array_size);
        return splice;
    }

    // Same as cspptrie.cpp ThisThreadID, but not a namespace-scope symbol.
    terark_pure_func static size_t ThisThreadID() {
#if defined(_MSC_VER) || !(defined(_M_X64) || defined(_M_IX86) || defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) || defined(__amd64))
        auto id = std::this_thread::get_id();
        using U = typename std::conditional<sizeof(id) == 4, unsigned int,
                                            unsigned long long>::type;
        return (size_t) (U&) (id);
#else
        // gnu pthread_self impl
        size_t __self;
        asm("movq %%fs:%c1,%q0"
            : "=r"(__self)
            : "i"(16));
        return __self;
#endif
    }

  public:
    template<class V = Value>
    static typename std::enable_if<!std::is_void<V>::value, V*>::type value(
        const char* key) {
        return &(reinterpret_cast<Node*>(const_cast<char*>(key)) - 1)->m_val;
    }

    static const uint16_t kMaxPossibleHeight = 20;
    static const uint32_t kIterTokenUpdateStride = 200;
    static const link_t nil = link_t(-1);
    static constexpr uint8_t kFlagGc = 1;
    static constexpr uint8_t kFlagReadonly = 2;

    explicit OffsetSkipList(Comparator cmp, size_t mem_cap,
                            int max_height = 14,
                            int branching_factor = 4)
        : m_max_height_limit(static_cast<uint16_t>(max_height)),
          m_branching(static_cast<uint16_t>(branching_factor)),
          m_scaled_inverse_branching(2147483648u / m_branching),
          m_mem(64),
          m_compare(cmp),
          m_max_height(1),
          m_head_loc(nil) {
        TERARK_VERIFY_GT(max_height, 0);
        TERARK_VERIFY_LE(max_height, kMaxPossibleHeight);
        TERARK_VERIFY_EQ(m_max_height_limit, static_cast<uint16_t>(max_height));
        TERARK_VERIFY_GT(branching_factor, 1);
        TERARK_VERIFY_GT(mem_cap, 0);
        InitDummy();
        InstallMempoolTls();
        m_mem.reserve(mem_cap);
        InitSplice();
        InitHead();
    }

    // Wrap existing mempool bytes (SST mmap). Does not allocate a head.
    OffsetSkipList(Comparator cmp, fstring mem, link_t head_loc, int max_height,
                   int branching, uint64_t num_nodes)
        : m_max_height_limit(static_cast<uint16_t>(max_height)),
          m_branching(static_cast<uint16_t>(branching)),
          m_scaled_inverse_branching(2147483648u / m_branching),
          m_mem(64),
          m_compare(cmp),
          m_own_mem(false),
          m_max_height(max_height),
          m_head_loc(nil) {
        TERARK_VERIFY_GT(branching, 1);
        TERARK_VERIFY(mem.data() != nullptr);
        TERARK_VERIFY_GT(mem.size(), 0);
        TERARK_VERIFY_GT(max_height, 0);
        TERARK_VERIFY_LE(max_height, kMaxPossibleHeight);
        TERARK_VERIFY_NE(head_loc, nil);
        TERARK_VERIFY_LT(size_t(head_loc) * AlignSize, mem.size());
        const size_t prefix = sizeof(link_t) * size_t(max_height - 1);
        TERARK_VERIFY_GE(size_t(head_loc) * AlignSize, prefix);
        TERARK_VERIFY_LE(size_t(head_loc) * AlignSize + sizeof(Node), mem.size());
        InitDummy();
        InstallMempoolTls();
        m_mem.risk_set_data(const_cast<byte_t*>(reinterpret_cast<const byte_t*>(mem.data())),
                            mem.size());
        InitSplice();
        m_head_loc = head_loc;
        m_num_nodes = num_nodes;
        m_flag |= kFlagReadonly;
    }

    // File-backed mmap pool (CSPP Patricia file_path). Allocates head.
    OffsetSkipList(Comparator cmp, size_t mem_cap, fstring file_path,
                   int max_height = 14, int branching_factor = 4)
        : m_max_height_limit(static_cast<uint16_t>(max_height)),
          m_branching(static_cast<uint16_t>(branching_factor)),
          m_scaled_inverse_branching(2147483648u / m_branching),
          m_mem(64),
          m_compare(cmp),
          m_own_mem(false),
          m_max_height(1),
          m_head_loc(nil) {
        TERARK_VERIFY_GT(max_height, 0);
        TERARK_VERIFY_LE(max_height, kMaxPossibleHeight);
        TERARK_VERIFY_EQ(m_max_height_limit, static_cast<uint16_t>(max_height));
        TERARK_VERIFY_GT(branching_factor, 1);
        TERARK_VERIFY_GT(mem_cap, 0);
        TERARK_VERIFY(!file_path.empty());
        InitDummy();
        InstallMempoolTls();
        try {
            size_t sz = mem_cap;
            void* p = mmap_write(file_path, &sz, &m_mmap_fd);
            m_mmap_size = sz;
            m_mmap_fpath = file_path.str();
            m_mem.risk_set_data(static_cast<byte_t*>(p), 0);
            m_mem.risk_set_capacity(sz);
            InitSplice();
            InitHead();
        } catch (...) {
            CloseFileMmap();
            if (!m_mmap_fpath.empty()) {
                ::remove(m_mmap_fpath.c_str());
            }
            throw;
        }
    }

    OffsetSkipList(const OffsetSkipList&) = delete;
    OffsetSkipList& operator=(const OffsetSkipList&) = delete;
    ~OffsetSkipList() {
        if (!is_readonly()) {
            SyncNumNodes();
        }
        if (m_own_mem || is_mmap()) {
            DrainAllLazyFree();
        }
        if (is_mmap()) {
            CloseFileMmap();
        } else if (!m_own_mem) {
            m_mem.risk_release_ownership();
        }
        ReleaseTlsTokensForDestroy();
        m_dummy.m_flags.state = ReleaseDone;
    }


    link_t head_loc() const { return m_head_loc; }
    const char* KeyOf(link_t loc) const {
        TERARK_ASSERT_NE(loc, nil);
        TERARK_ASSERT_NE(loc, m_head_loc);
        return Node::NodeAt(base(), loc)->Key();
    }
    int max_height() const { return GetMaxHeight(); }
    bool is_gc_enabled() const { return m_flag & kFlagGc; }
    void set_gc_enabled(bool v) {
        m_flag = uint8_t((m_flag & ~kFlagGc) | (v ? kFlagGc : 0));
    }
    terark_forceinline bool need_pin() const { return m_flag == kFlagGc; }
    int k_max_height() const { return m_max_height_limit; }
    int k_branching() const { return m_branching; }

    // Optional leading_len sits at the allocation start (varint+value), then
    // [higher nexts][Node][key]. Dup-fail sfree can drop the contiguous top.
    char* AllocateKey(size_t key_size, MemTls* tc, size_t leading_len = 0) {
        TERARK_ASSERT_EQ(is_readonly(), false);
        Node* x = AllocateNode(key_size, RandomHeight(), tc, leading_len);
        if (terark_unlikely(x == nullptr)) {
            return nullptr;
        }
        return const_cast<char*>(x->Key());
    }

    // Insert() returned an existing key: this node was never linked. Height
    // is still stashed in m_next[0]. key_size / leading_len must match
    // AllocateKey().
    void FreeUnusedKey(const char* key, size_t key_size, MemTls* tc,
                       size_t leading_len = 0) {
        const size_t pos = KeyAllocPos(key, leading_len);
        Node* x = reinterpret_cast<Node*>(const_cast<char*>(key)) - 1;
        const int height = x->UnstashHeight();
        const size_t prefix = sizeof(link_t) * size_t(height - 1);
        tls_sfree(pos, leading_len + prefix + sizeof(Node) + key_size, tc);
    }

    // Keep the leading_len bytes, sfree Node+key (the top). Returns
    // leading pos (same as KeyAllocPos).
    size_t FreeUnusedKeyKeepLeading(const char* key, size_t key_size,
                                    size_t leading_len, MemTls* tc) {
        TERARK_ASSERT_GT(leading_len, 0);
        TERARK_ASSERT_AL(leading_len, AlignSize);
        const size_t pos = KeyAllocPos(key, leading_len);
        Node* x = reinterpret_cast<Node*>(const_cast<char*>(key)) - 1;
        const int height = x->UnstashHeight();
        const size_t prefix = sizeof(link_t) * size_t(height - 1);
        const size_t drop = prefix + sizeof(Node) + key_size;
        TERARK_ASSERT_GE(drop, sizeof(link_t));
        tls_sfree(pos + leading_len, drop, tc);
        return pos;
    }

    size_t KeyAllocPos(const char* key, size_t leading_len = 0) const {
        Node* x = reinterpret_cast<Node*>(const_cast<char*>(key)) - 1;
        const int height = x->UnstashHeight();
        TERARK_ASSERT_GE(height, 1);
        TERARK_ASSERT_LE(height, m_max_height_limit);
        TERARK_ASSERT_AL(leading_len, AlignSize);
        const size_t prefix = sizeof(link_t) * size_t(height - 1);
        auto* start = reinterpret_cast<byte_t*>(x) - prefix - leading_len;
        const size_t pos = size_t(start - m_mem.data());
        TERARK_ASSERT_AL(pos, AlignSize);
        return pos;
    }


    // MultiReadMultiWrite token (CSPP versseq / rotate / lazy-free).
    enum TokenState : uint8_t { ReleaseDone,
                                AcquireDone,
                                AcquireIdle };
    struct TokenFlags {
        TokenState state;
        uint8_t is_head;
    };
    static_assert(sizeof(TokenFlags) == 2, "sizeof(TokenFlags) == 2");
    class Token {
      public:
        Token() {
            m_thread_id = ThisThreadID();
        }

        Token(const Token&) = delete;
        Token& operator=(const Token&) = delete;
        void acquire(OffsetSkipList* list) {
            TERARK_ASSERT_NE(list, nullptr);
            if (m_list != nullptr) {
                TERARK_ASSERT_EQ(m_list, list);
            }
            m_list = list;
            // Writable, or leftover queue after set_readonly: join/rotate.
            // Readonly and qlen==0: just flip state (CSPP TokenBase::acquire).
            if (!list->is_readonly() || list->m_token_qlen) {
                if (terark_unlikely(m_tls == nullptr) && !list->is_readonly()) {
                    init_tls();
                }
                mt_acquire();
            } else {
                switch (m_flags.state) {
                default:
                    TERARK_DIE("Bad TokenState = %d", int(m_flags.state));
                    break;
                case AcquireDone:
                    TERARK_DIE("AcquireDone == TokenState");
                    break;
                case AcquireIdle:
                case ReleaseDone:
                    m_flags.state = AcquireDone;
                    break;
                }
            }
        }

        void release() {
            if (!m_list->is_readonly() || m_list->m_token_qlen) {
                mt_release();
            } else {
                switch (m_flags.state) {
                default:
                    TERARK_DIE("Unknown TokenState");
                    break;
                case ReleaseDone:
                    TERARK_DIE("ReleaseDone == TokenState");
                    break;
                case AcquireIdle:
                case AcquireDone:
                    m_flags.state = ReleaseDone;
                    break;
                }
            }
        }

        void idle() {
            TERARK_ASSERT_EQ(m_flags.state, AcquireDone);
            if (!m_list->is_readonly()) {
                maybe_rotate(AcquireIdle);
            } else if (m_list->m_token_qlen == 0) {
                m_flags.state = AcquireIdle;
            } else {
                // After set_readonly: unlink so qlen can drain to 0.
                m_list->m_token_mtx.lock();
                if (terark_likely(m_next != nullptr)) {
                    m_flags = {AcquireIdle, 0};
                    remove_self();
                    m_next = m_prev = nullptr;
                    m_list->m_token_qlen--;
                } else {
                    m_flags.state = AcquireIdle;
                }
                m_list->m_token_mtx.unlock();
            }
        }

        void update() {
            TERARK_ASSERT_EQ(m_flags.state, AcquireDone);
            if (!m_list->is_readonly()) {
                maybe_rotate(AcquireDone);
            }
        }

        // Same lifetime as CSPP TokenBase::dispose: unlink if idle, then delete.
        // Never delete a token that is still AcquireDone (still in the queue).
        void dispose() {
            switch (m_flags.state) {
            default:
                TERARK_DIE("Unknown TokenState = %d", int(m_flags.state));
                break;
            case AcquireDone:
                TERARK_DIE("AcquireDone == TokenState");
                break;
            case AcquireIdle:
                release();
                // fallthrough
            case ReleaseDone:
                delete this;
                break;
            }
        }

        TokenState state() const { return m_flags.state; }

      protected:
        virtual ~Token() {
            // Dummy is a queue sentinel (InitDummy sets AcquireIdle). Ctor
            // unwind never reaches ~OffsetSkipList, which is the only place
            // that would set ReleaseDone on m_dummy.
            if (m_list != nullptr && this == &m_list->m_dummy) {
                return;
            }
            if (m_num_nodes && m_list != nullptr) {
                as_atomic(m_list->m_num_nodes)
                    .fetch_add(m_num_nodes, std::memory_order_relaxed);
                m_num_nodes = 0;
            }
            TERARK_VERIFY_EQ(m_flags.state, ReleaseDone);
        }

        OffsetSkipList* skiplist() const { return m_list; }

      private:
        friend class OffsetSkipList;
        friend struct LazyFreeListTLS;
        OffsetSkipList* m_list = nullptr;
        void* m_tls = nullptr;
        size_t m_thread_id = size_t(-1);
        // frequently sync with other threads
        Token* m_prev = nullptr;
        Token* m_next = nullptr;
        uint64_t m_verseq = 0;
        uint64_t m_min_verseq = 0;
        uint64_t m_num_nodes = 0;
        TokenFlags m_flags{ReleaseDone, 0};
        void init_tls() {
            m_thread_id = ThisThreadID();
            m_tls = m_list->m_mem.get_tls();
        }

        void mt_acquire() {
            switch (m_flags.state) {
            default:
                TERARK_DIE("Bad TokenState = %d", int(m_flags.state));
                break;
            case AcquireDone:
                TERARK_DIE("AcquireDone == TokenState");
                break;
            case AcquireIdle:
                maybe_rotate(AcquireDone);
                break;
            case ReleaseDone:
                m_list->m_token_mtx.lock();
                m_flags = {AcquireDone, uint8_t(m_list->m_token_qlen == 0)};
                m_list->m_token_qlen++;
                m_min_verseq = m_list->m_dummy.m_min_verseq;
                m_verseq = m_list->m_dummy.m_verseq++;
                add_to_back();
                m_list->m_token_mtx.unlock();
                break;
            }
        }

        void mt_release() {
            switch (m_flags.state) {
            default:
                TERARK_DIE("Unknown TokenState");
                break;
            case ReleaseDone:
                TERARK_DIE("ReleaseDone == TokenState");
                break;
            case AcquireDone:
            case AcquireIdle:
                m_list->m_token_mtx.lock();
                if (terark_likely(m_next != nullptr)) {
                    if (terark_unlikely(this == m_list->m_dummy.m_next)) {
                        m_next->m_flags.is_head = 1;
                        m_list->m_dummy.m_min_verseq = m_verseq;
                    }
                    remove_self();
                    m_next = m_prev = nullptr;
                    m_list->m_token_qlen--;
                }
                m_flags = {ReleaseDone, 0};
                m_list->m_token_mtx.unlock();
                break;
            }
        }

        void maybe_rotate(TokenState target) {
            if (terark_unlikely(m_flags.is_head && m_next != &m_list->m_dummy)) {
                rotate(target);
            } else {
                m_flags.state = target;
            }
        }

        void rotate(TokenState target) {
            m_list->m_token_mtx.lock();
            remove_self();
            add_to_back();
            m_min_verseq = m_list->m_dummy.m_min_verseq = m_verseq;
            m_verseq = m_list->m_dummy.m_verseq++;
            m_flags = {target, 0};
            m_list->m_dummy.m_next->m_flags.is_head = 1;
            m_list->m_token_mtx.unlock();
        }

        void remove_self() {
            m_prev->m_next = m_next;
            m_next->m_prev = m_prev;
        }

        void add_to_back() {
            m_next = &m_list->m_dummy;
            m_prev = m_list->m_dummy.m_prev;
            m_next->m_prev = this;
            m_prev->m_next = this;
        }
    };

    terark_forceinline MemTls* tls_get() const {
        auto* mp = const_cast<ThreadCacheMemPool<AlignSize>*>(&m_mem);
        auto* lzf = static_cast<MemTls*>(mp->get_tls());
        TERARK_VERIFY_NE(lzf, nullptr);
        return lzf;
    }

    template<class TokenType = Token>
    terark_forceinline TokenType* tls_token_nn() const {
        return tls_get()->template get_token<TokenType>();
    }

    Token* tls_token() const { return tls_token_nn<Token>(); }

    void for_each_tls_token(std::function<void(Token*)> fn) const {
        auto* mp = const_cast<ThreadCacheMemPool<AlignSize>*>(&m_mem);
        mp->for_each_tls([&fn](TCMemPoolOneThread<AlignSize>* tc) {
            auto* lzf = static_cast<LazyFreeListTLS*>(tc);
            if (lzf->writer) {
                fn(lzf->writer);
            }
        });
    }

    void LazyFree(size_t pos, size_t len, Token* token) {
        TERARK_ASSERT_NE(token->m_tls, nullptr);
        auto* lzf = static_cast<LazyFreeListTLS*>(token->m_tls);
        lzf->q.push_back({token->m_verseq, pos, len});
        lzf->mem_size += len;
    }

    void GC(Token* token) {
        TERARK_ASSERT_NE(token->m_tls, nullptr);
        auto* lzf = static_cast<LazyFreeListTLS*>(token->m_tls);
        RevokeExpired(*lzf, token->m_min_verseq, false);
    }

    void GCAll() {
        const uint64_t min_verseq = m_dummy.m_min_verseq;
        m_mem.for_each_tls([this, min_verseq](TCMemPoolOneThread<AlignSize>* tc) {
            RevokeExpired(*static_cast<LazyFreeListTLS*>(tc), min_verseq, true);
        });
    }

    uint64_t num_nodes() const { return m_num_nodes; }
    uint64_t slow_exact_num_nodes() const {
        if (is_readonly()) {
            return m_num_nodes;
        }
        uint64_t n = as_atomic(m_num_nodes).load(std::memory_order_relaxed);
        for_each_tls_token([&n](Token* t) { n += t->m_num_nodes; });
        return n;
    }
    size_t lazy_free_bytes() const {
        size_t n = 0;
        const_cast<ThreadCacheMemPool<AlignSize>&>(m_mem).for_each_tls(
            [&n](TCMemPoolOneThread<AlignSize>* tc) {
                n += static_cast<LazyFreeListTLS*>(tc)->mem_size;
            });
        return n;
    }


    // Sequential only: shared m_seq_splice (InlineSkipList::seq_splice_).
    // Concurrent callers use InsertConcurrently. Caller passes the TLS
    // already obtained for alloc; this does not get_tls again.
    char* Insert(char* key, MemTls* tc) {
        return Insert<false>(key, &m_seq_splice, false, tc);
    }

    char* InsertWithHint(char* key, MemTls* tc) {
        TERARK_ASSERT_NE(tc, nullptr);
        return Insert<false>(key, tc->get_splice(), true, tc);
    }

    // Reset height. Splice stays on TLS until ~LazyFreeListTLS.
    void FinishHint(MemTls* tc) {
        TERARK_ASSERT_NE(tc, nullptr);
        tc->assert_current_thread();
        Splice* splice = tc->peek_splice();
        TERARK_ASSERT_NE(splice, nullptr);
        splice->height = 0;
    }

    char* InsertConcurrently(char* key, MemTls* tc) {
        link_t prev[kMaxPossibleHeight + 1];
        link_t next[kMaxPossibleHeight + 1];
        Splice splice;
        splice.prev = prev;
        splice.next = next;
        return Insert<true>(key, &splice, false, tc);
    }

    char* InsertWithHintConcurrently(char* key, MemTls* tc) {
        TERARK_ASSERT_NE(tc, nullptr);
        return Insert<true>(key, tc->get_splice(), true, tc);
    }

  private:
    void AccountNewNode(MemTls* tc) {
        if (is_gc_enabled()) {
            TERARK_ASSERT_NE(tc, nullptr);
            TERARK_ASSERT_NE(tc->writer, nullptr);
            tc->writer->m_num_nodes++;
        } else {
            as_atomic(m_num_nodes).fetch_add(1, std::memory_order_relaxed);
        }
    }

    template<bool UseCAS>
    char* Insert(char* key, Splice* splice,
                 bool allow_partial_splice_fix, MemTls* tc) {
        TERARK_ASSERT_EQ(is_readonly(), false);
        TERARK_ASSERT_NE(tc, nullptr);
        tc->assert_current_thread();
        Node* x = reinterpret_cast<Node*>(key) - 1;
        const DecodedKey key_decoded = m_compare.decode_key(key);
        int height = x->UnstashHeight();
        TERARK_ASSERT_BE(height, 1, m_max_height_limit);
        int max_height = GrowMaxHeight(height);
        TERARK_ASSERT_LE(max_height, kMaxPossibleHeight);
        int recompute_height =
            PrepareSplice(key_decoded, splice, allow_partial_splice_fix,
                          max_height);
        char* exist = LinkFromSplice<UseCAS>(
            x, key_decoded, height, splice, recompute_height);
        if (exist == nullptr) {
            AccountNewNode(tc);
        }
        return exist;
    }

  public:
    // Node [len][ukey] -> DecodedKey. Search APIs take DecodedKey only;
    // a raw node pointer must not be passed (Slice(const char*) is a C string).
    DecodedKey DecodeKey(const char* key) const {
        return m_compare.decode_key(key);
    }
    const char* Get(const char*) const = delete;
    bool Contains(const char*) const = delete;
    uint64_t EstimateCount(const char*) const = delete;
    std::pair<link_t, int> FindGreaterOrEqual(const char*) const = delete;

    // nullptr: absent. Non-null: that node's key (equal).
    const char* Get(const DecodedKey& key) const {
        auto found = FindGreaterOrEqual(key);
        if (found.first != nil && found.second == 0) {
            return KeyOf(found.first);
        }
        return nullptr;
    }

    bool Contains(const DecodedKey& key) const {
        return Get(key) != nullptr;
    }
    uint64_t EstimateCount(const DecodedKey& key_decoded) const {
        uint64_t count = 0;
        byte_t* b = base();
        link_t xloc = m_head_loc;
        Node* x = NodeAt(b, xloc);
        int level = GetMaxHeight() - 1;
        while (true) {
            if (xloc != m_head_loc) {
                TERARK_ASSERT_LT(m_compare(x->Key(), key_decoded), 0);
            }
            link_t next_loc = x->NextLoc(level);
            if (next_loc != nil) {
                Node* next = NodeAt(b, next_loc);
                PrefetchLoc(b, next->NextLoc(level));
                if (m_compare(next->Key(), key_decoded) < 0) {
                    xloc = next_loc;
                    x = next;
                    count++;
                    continue;
                }
            }
            if (level == 0) {
                return count;
            }
            count *= m_branching;
            level--;
        }
    }

    // second is this hop's compare, or +1 when next_loc is nil / last_bigger
    // (already known greater; not the raw Comparator result).
    std::pair<link_t, int> FindGreaterOrEqual(const DecodedKey& key_decoded) const {
        byte_t* b = base();
        link_t xloc = m_head_loc;
        Node* x = NodeAt(b, xloc);
        int level = GetMaxHeight() - 1;
        link_t last_bigger = nil;
        while (true) {
            link_t next_loc = x->NextLoc(level);
            if (next_loc != nil && next_loc != last_bigger) {
                Node* next = NodeAt(b, next_loc);
                PrefetchLoc(b, next->NextLoc(level));
                if (xloc != m_head_loc) {
                    TERARK_ASSERT_LT(m_compare(x->Key(), next->Key()), 0);
                    TERARK_ASSERT_LT(m_compare(x->Key(), key_decoded), 0);
                }
                int cmp = m_compare(next->Key(), key_decoded);
                if (terark_unlikely(cmp == 0 || (cmp > 0 && level == 0))) {
                    return {next_loc, cmp};
                } else if (cmp < 0) {
                    xloc = next_loc;
                    x = next;
                    continue;
                }
            } else if (xloc != m_head_loc) {
                TERARK_ASSERT_LT(m_compare(x->Key(), key_decoded), 0);
            }
            if (level == 0) {
                return {next_loc, 1};
            }
            last_bigger = next_loc;
            level--;
        }
    }

    void TEST_Validate() const {
        byte_t* b = base();
        link_t nodes[kMaxPossibleHeight];
        int max_height = GetMaxHeight();
        TERARK_ASSERT_GT(max_height, 0);
        for (int i = 0; i < max_height; i++) {
            nodes[i] = m_head_loc;
        }
        while (true) {
            link_t l0_next = NodeAt(b, nodes[0])->NextLoc(0);
            if (l0_next == nil) {
                break;
            }
            if (nodes[0] != m_head_loc) {
                TERARK_ASSERT_LT(m_compare(KeyOf(nodes[0]), KeyOf(l0_next)), 0);
            }
            nodes[0] = l0_next;
            int i = 1;
            while (i < max_height) {
                link_t next = NodeAt(b, nodes[i])->NextLoc(i);
                if (next == nil) {
                    break;
                }
                if (m_compare.equal(KeyOf(nodes[0]), KeyOf(next))) {
                    TERARK_ASSERT_EQ(next, nodes[0]);
                    nodes[i] = next;
                } else {
                    TERARK_ASSERT_LT(m_compare(KeyOf(nodes[0]), KeyOf(next)), 0);
                    break;
                }
                i++;
            }
        }
        for (int i = 1; i < max_height; i++) {
            TERARK_ASSERT_NE(nodes[i], nil);
            TERARK_ASSERT_EQ(NodeAt(b, nodes[i])->NextLoc(i), nil);
        }
    }

    Splice* TEST_AllocHint() { return tls_get()->get_splice(); }

    char* TEST_AllocateKeyWithHeight(size_t key_size, int height, MemTls* tc,
                                     size_t leading_len = 0) {
        TERARK_VERIFY_GE(height, 1);
        TERARK_VERIFY_LE(height, m_max_height_limit);
        Node* x = AllocateNode(key_size, height, tc, leading_len);
        return x ? const_cast<char*>(x->Key()) : nullptr;
    }

    link_t TEST_LocOf(const char* key) const {
        Node* x = reinterpret_cast<Node*>(const_cast<char*>(key)) - 1;
        return LocOf(base(), x);
    }

    void TEST_FindSpliceForLevel(const char* key, link_t before, link_t after,
                                 int level, link_t* out_prev, link_t* out_next) {
        FindSpliceForLevel<false>(m_compare.decode_key(key), before, after, level,
                                  out_prev, out_next);
    }

    // Bypass PrepareSplice so a deliberately stale Splice reaches LinkFromSplice.
    char* TEST_InsertSkipPrepare(char* key, Splice* splice, MemTls* tc) {
        TERARK_VERIFY(splice != nullptr);
        TERARK_ASSERT_NE(tc, nullptr);
        Node* x = reinterpret_cast<Node*>(key) - 1;
        const DecodedKey key_decoded = m_compare.decode_key(key);
        int height = x->UnstashHeight();
        GrowMaxHeight(height);
        char* exist = LinkFromSplice<false, true>(
            x, key_decoded, height, splice, 0);
        if (exist == nullptr) {
            AccountNewNode(tc);
        }
        return exist;
    }

    // Pre-fix FindSpliceForLevel: stop when next == after even if after < key.
    void TEST_FindSpliceForLevelLegacy(const char* key, link_t before_loc,
                                       link_t after_loc, int level,
                                       link_t* out_prev, link_t* out_next) {
        const DecodedKey decoded = m_compare.decode_key(key);
        byte_t* b = base();
        link_t before = before_loc;
        if (before == nil) {
            before = m_head_loc;
        }
        Node* bn = NodeAt(b, before);
        while (true) {
            link_t next = bn->NextLoc(level);
            if (next == after_loc || !KeyIsAfterLoc(decoded, next, b)) {
                *out_prev = before;
                *out_next = next;
                return;
            }
            before = next;
            bn = NodeAt(b, next);
        }
    }

    // Pre-fix LinkFromSplice (non-CAS, skip Prepare): treat splice locs as
    // stable. Only next-equal is duplicate; prev > key / next < key
    // are asserted then linked anyway (release) or abort (debug).
    char* TEST_InsertSkipPrepareLegacy(char* key, Splice* splice, MemTls* tc) {
        TERARK_VERIFY(splice != nullptr);
        TERARK_ASSERT_NE(tc, nullptr);
        Node* x = reinterpret_cast<Node*>(key) - 1;
        const DecodedKey key_decoded = m_compare.decode_key(key);
        int height = x->UnstashHeight();
        GrowMaxHeight(height);
        byte_t* b = base();
        const link_t xloc = LocOf(b, x);
        for (int i = 0; i < height; ++i) {
            link_t next_loc = splice->next[i];
            if (i == 0 && next_loc != nil &&
                m_compare.equal(KeyOf(next_loc), key_decoded)) {
                x->StashHeight(height);
                return const_cast<char*>(KeyOf(next_loc));
            }
            x->NoBarrier_SetNextLoc(i, next_loc);
            NodeAt(b, splice->prev[i])->SetNextLoc(i, xloc);
        }
        AccountNewNode(tc);
        return nullptr;
    }


    size_t mem_size() const { return m_mem.size(); }
    size_t mem_capacity() const { return m_mem.capacity(); }
    size_t mem_align_size() const { return AlignSize; }
    const byte_t* mem_data() const { return m_mem.data(); }
    ThreadCacheMemPool<AlignSize>& mempool() { return m_mem; }
    const ThreadCacheMemPool<AlignSize>& mempool() const { return m_mem; }

    size_t tls_alloc(size_t n, MemTls* tc) {
        TERARK_VERIFY_NE(tc, nullptr);
        return m_mem.alloc(n, tc);
    }
    void tls_sfree(size_t pos, size_t len, MemTls* tc) {
        TERARK_VERIFY_NE(tc, nullptr);
        m_mem.sfree(pos, len, tc);
    }

    intptr_t mmap_fd() const { return m_mmap_fd; }
    const std::string& mmap_fpath() const { return m_mmap_fpath; }
    bool is_mmap() const { return m_mmap_fd >= 0; }
    bool is_readonly() const { return m_flag & kFlagReadonly; }
    uint32_t token_qlen() const { return m_token_qlen; }
    fstring get_mmap() const {
        return is_mmap() ? fstring(reinterpret_cast<const char*>(m_mem.data()),
                                   m_mem.capacity())
                         : fstring();
    }
    // File mmap: drop unused tail pages and ftruncate to used size
    // (Patricia mempool_set_readonly, MWMR only).
    void set_readonly() {
        if (m_flag & kFlagReadonly) {
            return;
        }
        if (m_mmap_fd >= 0) {
            byte_t* base = const_cast<byte_t*>(m_mem.data());
            const size_t used = m_mem.size();
            const size_t cap = m_mem.capacity();
            TERARK_VERIFY_LE(used, cap);
            // Shrink the pool first so a later munmap/ftruncate failure
            // cannot hand new allocs an unmapped tail.
            m_mem.risk_set_capacity(used);
#if defined(_MSC_VER)
            (void) base;
#else
            const size_t aligned = pow2_align_up(used, size_t(4096));
            if (cap > aligned) {
                ::munmap(base + aligned, cap - aligned);
            }
            m_mmap_size = std::min(cap, aligned);
            while (::ftruncate(int(m_mmap_fd), off_t(used)) < 0) {
                if (EINTR == errno) {
                    std::this_thread::yield();
                    continue;
                }
                THROW_STD(runtime_error, "ftruncate(%s, %zd) = %s", m_mmap_fpath.c_str(),
                          used, strerror(errno));
            }
#endif
        }
        SyncNumNodes();
        m_flag |= kFlagReadonly;
    }


    class IteratorReadonlyBase {
      protected:
        OffsetSkipList* m_list = nullptr;
        OffsetSkipList* skiplist() const { return m_list; }
    };
    class IteratorWritableBase : public Token {
      protected:
        uint32_t m_scan_steps = 0;
        void UpdateToken() {
            this->update();
            m_scan_steps = 0;
        }
        void MaybeUpdateTokenOnScan() {
            if (++m_scan_steps >= kIterTokenUpdateStride) {
                this->update();
                m_scan_steps = 0;
            }
        }
    };

    template<bool NoPin>
    class IteratorTpl : public std::conditional_t<NoPin, IteratorReadonlyBase, IteratorWritableBase> {
        static constexpr bool NeedToken = !NoPin;
      public:
        IteratorTpl(const IteratorTpl&) = delete;
        IteratorTpl& operator=(const IteratorTpl&) = delete;

        explicit IteratorTpl(const OffsetSkipList* list) {
            TERARK_ASSERT_NE(list, nullptr);
            if constexpr (NoPin) {
                TERARK_VERIFY(!list->need_pin());
                this->m_list = const_cast<OffsetSkipList*>(list);
            } else {
                this->acquire(const_cast<OffsetSkipList*>(list));
            }
        }
        ~IteratorTpl() {
            if constexpr (NeedToken) {
                if (this->state() == AcquireDone || this->state() == AcquireIdle) {
                    this->release();
                }
            }
        }

        bool Valid() const { return m_loc != nil; }
        const char* key() const {
            TERARK_ASSERT_NE(m_loc, nil);
            return this->skiplist()->KeyOf(m_loc);
        }

        void Next() {
            TERARK_ASSERT_NE(m_loc, nil);
            if constexpr (NeedToken) {
                TERARK_ASSERT_EQ(this->state(), AcquireDone);
            }
            m_loc = NodeAt(this->skiplist()->base(), m_loc)->NextLoc(0);
            if constexpr (NeedToken) {
                this->MaybeUpdateTokenOnScan();
            }
        }

        void Prev() {
            TERARK_ASSERT_NE(m_loc, nil);
            m_loc = this->skiplist()->FindLessThan(
                this->skiplist()->DecodeKey(this->skiplist()->KeyOf(m_loc)));
            if (m_loc == this->skiplist()->m_head_loc) {
                m_loc = nil;
            }
            if constexpr (NeedToken) {
                this->MaybeUpdateTokenOnScan();
            }
        }

        void Seek(const char*) = delete;
        void SeekForPrev(const char*) = delete;

        void Seek(const DecodedKey& target) {
            if constexpr (NeedToken) {
                this->UpdateToken();
            }
            m_loc = this->skiplist()->FindGreaterOrEqual(target).first;
        }

        void SeekForPrev(const DecodedKey& target) {
            if constexpr (NeedToken) {
                this->UpdateToken();
            }
            OffsetSkipList* sl = this->skiplist();
            link_t pred = sl->FindLessThan(target);
            link_t succ = NodeAt(sl->base(), pred)->NextLoc(0);
            // FindLessThan's pred can be stale: a node may already sit in
            // (pred, target]. Testing succ == target missed those and fell
            // back to pred. Accept succ <= target and walk L0 to the last
            // key <= target. Do not return the stale pred in that case.
            if (succ != nil && sl->m_compare(sl->KeyOf(succ), target) <= 0) {
                m_loc = succ;
                byte_t* b = sl->base();
                while (true) {
                    link_t n = NodeAt(b, m_loc)->NextLoc(0);
                    if (n == nil || sl->m_compare(sl->KeyOf(n), target) > 0) {
                        break;
                    }
                    m_loc = n;
                }
            } else if (pred != sl->m_head_loc) {
                m_loc = pred;
            } else {
                m_loc = nil;
            }
        }

        void RandomSeek() {
            if constexpr (NeedToken) {
                this->UpdateToken();
            }
            m_loc = this->skiplist()->FindRandomEntry();
        }

        void SeekToFirst() {
            if constexpr (NeedToken) {
                this->UpdateToken();
            }
            m_loc = NodeAt(this->skiplist()->base(), this->skiplist()->m_head_loc)
                        ->NextLoc(0);
        }

        void SeekToLast() {
            if constexpr (NeedToken) {
                this->UpdateToken();
            }
            m_loc = this->skiplist()->FindLast();
            if (m_loc == this->skiplist()->m_head_loc) {
                m_loc = nil;
            }
        }

        void ResetKey(const char* key) {
            if constexpr (NeedToken) {
                TERARK_ASSERT_EQ(this->state(), AcquireDone);
            }
            if (key) {
                Node* x = reinterpret_cast<Node*>(const_cast<char*>(key)) - 1;
                m_loc = LocOf(this->skiplist()->base(), x);
            } else {
                m_loc = nil;
            }
        }

      private:
        link_t m_loc = nil;
    };
    using Iterator = IteratorTpl<false>;
    using NoPinIterator = IteratorTpl<true>;

  private:
    static link_t LocOf(const byte_t* base, const Node* n) {
        return Node::LocOf(base, n);
    }
    static Node* NodeAt(byte_t* base, link_t loc) {
        return Node::NodeAt(base, loc);
    }
    static void PrefetchLoc(const byte_t* b, link_t loc) {
        if (loc != nil) {
            TERARK_CPU_PREFETCH(b + size_t(loc) * AlignSize);
        }
    }

    byte_t* base() const { return const_cast<byte_t*>(m_mem.data()); }

    int GetMaxHeight() const {
        return m_max_height.load(std::memory_order_relaxed);
    }

    // Same Park–Miller reduction as rocksdb Random::Next(); avoid `% M`.
    static uint32_t NextRand() {
        thread_local uint32_t seed = 1;
        constexpr uint32_t M = 2147483647u;  // 2^31-1; 2^31 ≡ 1 (mod M)
        const uint64_t product = uint64_t(seed) * 16807u;
        seed = static_cast<uint32_t>((product >> 31) + (product & M));
        if (seed > M) {
            seed -= M;
        }
        return seed;
    }

    int RandomHeight() {
        int height = 1;
        while (height < m_max_height_limit &&
               NextRand() < m_scaled_inverse_branching) {
            height++;
        }
        TERARK_ASSERT_GT(height, 0);
        TERARK_ASSERT_LE(height, m_max_height_limit);
        return height;
    }


    Node* AllocateNode(size_t key_size, int height, MemTls* tc,
                       size_t leading_len = 0) {
        TERARK_ASSERT_AL(leading_len, AlignSize);
        const size_t prefix = sizeof(link_t) * size_t(height - 1);
        const size_t nbytes = leading_len + prefix + sizeof(Node) + key_size;
        size_t pos = tls_alloc(nbytes, tc);
        if (terark_unlikely(pos == size_t(-1))) {
            return nullptr;
        }
        TERARK_ASSERT_AL(pos, AlignSize);
        TERARK_ASSERT_AL(prefix, AlignSize);
        Node* x = reinterpret_cast<Node*>(m_mem.data() + pos + leading_len + prefix);
        x->StashHeight(height);
        if constexpr (!std::is_void<Value>::value) {
            new (&x->m_val) Value();
        }
        return x;
    }

    bool KeyIsAfterLoc(const DecodedKey& key, link_t loc, byte_t* b) const {
        if (loc == nil) {
            return false;
        }
        TERARK_ASSERT_NE(loc, m_head_loc);
        return m_compare(NodeAt(b, loc)->Key(), key) < 0;
    }

    link_t FindLessThan(const DecodedKey& key_decoded) const {
        byte_t* b = base();
        link_t xloc = m_head_loc;
        Node* x = NodeAt(b, xloc);
        int level = GetMaxHeight() - 1;
        link_t last_not_after = nil;
        while (true) {
            link_t next_loc = x->NextLoc(level);
            if (next_loc != nil && next_loc != last_not_after) {
                Node* next = NodeAt(b, next_loc);
                PrefetchLoc(b, next->NextLoc(level));
                if (xloc != m_head_loc) {
                    TERARK_ASSERT_LT(m_compare(x->Key(), next->Key()), 0);
                    TERARK_ASSERT_LT(m_compare(x->Key(), key_decoded), 0);
                }
                if (m_compare(next->Key(), key_decoded) < 0) {
                    xloc = next_loc;
                    x = next;
                    continue;
                }
            } else if (xloc != m_head_loc) {
                TERARK_ASSERT_LT(m_compare(x->Key(), key_decoded), 0);
            }
            if (level == 0) {
                return xloc;
            }
            last_not_after = next_loc;
            level--;
        }
    }

    link_t FindLast() const {
        byte_t* b = base();
        link_t xloc = m_head_loc;
        Node* x = NodeAt(b, xloc);
        int level = GetMaxHeight() - 1;
        while (true) {
            link_t next_loc = x->NextLoc(level);
            if (next_loc == nil) {
                if (level == 0) {
                    return xloc;
                }
                level--;
            } else {
                xloc = next_loc;
                x = NodeAt(b, next_loc);
            }
        }
    }

    link_t FindRandomEntry() const {
        byte_t* b = base();
        link_t xloc = m_head_loc;
        link_t limit_loc = nil;
        std::vector<link_t> lvl_nodes;
        int level = GetMaxHeight() - 1;
        while (level >= 0) {
            lvl_nodes.clear();
            link_t scan = xloc;
            while (scan != limit_loc) {
                lvl_nodes.push_back(scan);
                scan = NodeAt(b, scan)->NextLoc(level);
            }
            uint32_t rnd_idx = NextRand() % static_cast<uint32_t>(lvl_nodes.size());
            xloc = lvl_nodes[rnd_idx];
            if (rnd_idx + 1 < lvl_nodes.size()) {
                limit_loc = lvl_nodes[rnd_idx + 1];
            }
            level--;
        }
        return xloc == m_head_loc ? NodeAt(b, m_head_loc)->NextLoc(0) : xloc;
    }


    template<bool prefetch_before, bool trust_after = false>
    void FindSpliceForLevel(const DecodedKey& key, link_t before_loc,
                            link_t after_loc, int level, link_t* out_prev,
                            link_t* out_next) {
        byte_t* b = base();
        if constexpr (trust_after) {
            if (before_loc == nil) {
                before_loc = m_head_loc;
                after_loc = nil;
            }
        } else if (before_loc == nil ||
                   (before_loc != m_head_loc && !KeyIsAfterLoc(key, before_loc, b))) {
            before_loc = m_head_loc;
            after_loc = nil;
        }
        Node* before = NodeAt(b, before_loc);
        while (true) {
            link_t next_loc = before->NextLoc(level);
            if (next_loc != nil) {
                Node* next = NodeAt(b, next_loc);
                PrefetchLoc(b, next->NextLoc(level));
                if (prefetch_before && level > 0) {
                    PrefetchLoc(b, next->NextLoc(level - 1));
                }
                if (before_loc != m_head_loc) {
                    TERARK_ASSERT_LT(m_compare(before->Key(), next->Key()), 0);
                    TERARK_ASSERT_LT(m_compare(before->Key(), key), 0);
                }
                if ((trust_after && next_loc == after_loc) ||
                    m_compare(next->Key(), key) >= 0) {
                    *out_prev = before_loc;
                    *out_next = next_loc;
                    return;
                }
                before_loc = next_loc;
                before = next;
            } else {
                if (before_loc != m_head_loc) {
                    TERARK_ASSERT_LT(m_compare(before->Key(), key), 0);
                }
                *out_prev = before_loc;
                *out_next = nil;
                return;
            }
        }
    }

    void RecomputeSpliceLevels(const DecodedKey& key, Splice* splice,
                               int recompute_level) {
        TERARK_ASSERT_GT(recompute_level, 0);
        TERARK_ASSERT_LE(recompute_level, splice->height);
        for (int i = recompute_level - 1; i >= 0; --i) {
            FindSpliceForLevel<true, true>(key, splice->prev[i + 1],
                                           splice->next[i + 1], i,
                                           &splice->prev[i], &splice->next[i]);
        }
    }

    int GrowMaxHeight(int height) {
        int max_height = m_max_height.load(std::memory_order_relaxed);
        while (height > max_height) {
            if (m_max_height.compare_exchange_weak(max_height, height)) {
                max_height = height;
                break;
            }
        }
        return max_height;
    }

    int PrepareSplice(const DecodedKey& key, Splice* splice,
                      bool allow_partial_splice_fix, int max_height) {
        byte_t* b = base();
        int recompute_height = 0;
        if (splice->height < max_height) {
            splice->prev[max_height] = m_head_loc;
            splice->next[max_height] = nil;
            splice->height = max_height;
            recompute_height = max_height;
        } else {
            while (recompute_height < max_height) {
                link_t prev_loc = splice->prev[recompute_height];
                Node* prev = NodeAt(b, prev_loc);
                if (prev->NextLoc(recompute_height) !=
                    splice->next[recompute_height]) {
                    ++recompute_height;
                } else if (prev_loc != m_head_loc &&
                           m_compare(prev->Key(), key) >= 0) {
                    if (allow_partial_splice_fix) {
                        link_t bad = prev_loc;
                        while (splice->prev[recompute_height] == bad) {
                            ++recompute_height;
                        }
                    } else {
                        recompute_height = max_height;
                    }
                } else if (KeyIsAfterLoc(key, splice->next[recompute_height], b)) {
                    if (allow_partial_splice_fix) {
                        link_t bad = splice->next[recompute_height];
                        while (splice->next[recompute_height] == bad) {
                            ++recompute_height;
                        }
                    } else {
                        recompute_height = max_height;
                    }
                } else {
                    break;
                }
            }
        }
        TERARK_ASSERT_LE(recompute_height, max_height);
        if (recompute_height > 0) {
            RecomputeSpliceLevels(key, splice, recompute_height);
        }
        return recompute_height;
    }

    // RepairStale: TEST_InsertSkipPrepare only. Production Insert always
    // PrepareSplice first; then this matches InlineSkipList (level-0
    // compare for dups, no per-level order rescan from head).
    template<bool UseCAS, bool RepairStale = false>
    char* LinkFromSplice(Node* x, const DecodedKey& key, int height,
                         Splice* splice, [[maybe_unused]] int recompute_height) {
        byte_t* b = base();
        const link_t xloc = LocOf(b, x);
        bool splice_is_valid = true;
        auto fail_dup = [&](link_t found) -> char* {
            x->StashHeight(height);
            return const_cast<char*>(KeyOf(found));
        };
        auto dup_at_level0 = [&](link_t prev_loc, link_t next_loc,
                                 Node* prev) -> char* {
            if (terark_unlikely(next_loc != nil &&
                                m_compare(NodeAt(b, next_loc)->Key(), key) == 0)) {
                return fail_dup(next_loc);
            }
            if (terark_unlikely(prev_loc != m_head_loc &&
                                m_compare(prev->Key(), key) == 0)) {
                return fail_dup(prev_loc);
            }
            return nullptr;
        };
        auto repair_if_stale = [&](int i, link_t prev_loc, link_t next_loc) {
            if constexpr (!RepairStale) {
                return false;
            } else if ((next_loc != nil &&
                        m_compare(NodeAt(b, next_loc)->Key(), key) < 0) ||
                       (prev_loc != m_head_loc &&
                        m_compare(NodeAt(b, prev_loc)->Key(), key) > 0)) {
                FindSpliceForLevel<false>(key, m_head_loc, nil, i, &splice->prev[i],
                                          &splice->next[i]);
                return true;
            } else {
                return false;
            }
        };
        if constexpr (UseCAS) {
            for (int i = 0; i < height; ++i) {
                while (true) {
                    link_t next_loc = splice->next[i];
                    link_t prev_loc = splice->prev[i];
                    Node* prev = NodeAt(b, prev_loc);
                    if (i == 0) {
                        if (char* d = dup_at_level0(prev_loc, next_loc, prev)) {
                            return d;
                        }
                    }
                    if (repair_if_stale(i, prev_loc, next_loc)) {
                        if (i > 0) {
                            splice_is_valid = false;
                        }
                        continue;
                    }
                    if (next_loc != nil) {
                        TERARK_ASSERT_GT(m_compare(NodeAt(b, next_loc)->Key(), key),
                                         0);
                    }
                    if (prev_loc != m_head_loc) {
                        TERARK_ASSERT_LT(m_compare(prev->Key(), key), 0);
                    }
                    x->NoBarrier_SetNextLoc(i, next_loc);
                    if (prev->CASNextLoc(i, next_loc, xloc)) {
                        break;
                    }
                    FindSpliceForLevel<false>(key, prev_loc, nil, i, &splice->prev[i],
                                              &splice->next[i]);
                    if (i > 0) {
                        splice_is_valid = false;
                    }
                }
            }
        } else {
            for (int i = 0; i < height; ++i) {
                link_t prev_loc = splice->prev[i];
                Node* prev = NodeAt(b, prev_loc);
                if (i >= recompute_height && prev->NextLoc(i) != splice->next[i]) {
                    FindSpliceForLevel<false>(key, prev_loc, nil, i, &splice->prev[i],
                                              &splice->next[i]);
                    prev_loc = splice->prev[i];
                    prev = NodeAt(b, prev_loc);
                }
                link_t next_loc = splice->next[i];
                if (i == 0) {
                    if (char* d = dup_at_level0(prev_loc, next_loc, prev)) {
                        return d;
                    }
                }
                if (repair_if_stale(i, prev_loc, next_loc)) {
                    prev_loc = splice->prev[i];
                    next_loc = splice->next[i];
                    prev = NodeAt(b, prev_loc);
                    if (i == 0) {
                        if (char* d = dup_at_level0(prev_loc, next_loc, prev)) {
                            return d;
                        }
                    }
                }
                if (next_loc != nil) {
                    TERARK_ASSERT_GT(m_compare(NodeAt(b, next_loc)->Key(), key), 0);
                }
                if (prev_loc != m_head_loc) {
                    TERARK_ASSERT_LT(m_compare(prev->Key(), key), 0);
                }
                TERARK_ASSERT_EQ(prev->NextLoc(i), next_loc);
                x->NoBarrier_SetNextLoc(i, next_loc);
                prev->SetNextLoc(i, xloc);
            }
        }
        if (splice_is_valid) {
            for (int i = 0; i < height; ++i) {
                splice->prev[i] = xloc;
            }
        } else {
            splice->height = 0;
        }
        return nullptr;
    }


    const uint16_t m_max_height_limit;
    const uint16_t m_branching;
    const uint32_t m_scaled_inverse_branching;

    ThreadCacheMemPool<AlignSize> m_mem;
    Comparator const m_compare;
    intptr_t m_mmap_fd = -1;
    size_t m_mmap_size = 0;
    bool m_own_mem = true;
    uint8_t m_flag = kFlagGc;
    std::string m_mmap_fpath;

    // frequently updating
    mutable Token m_dummy;
    mutable uint32_t m_token_qlen = 0;
    std::atomic<int> m_max_height;
    mutable std::mutex m_token_mtx;
    link_t m_head_loc;
    uint64_t m_num_nodes = 0;

    Splice m_seq_splice;
    link_t m_seq_prev[kMaxPossibleHeight + 1];
    link_t m_seq_next[kMaxPossibleHeight + 1];

    struct LazyFreeItem {
        uint64_t age;
        size_t pos;
        size_t len;
    };
    struct LazyFreeListTLS : TCMemPoolOneThread<AlignSize> {
        friend class OffsetSkipList;
        explicit LazyFreeListTLS(OffsetSkipList* l)
            : TCMemPoolOneThread<AlignSize>(&l->m_mem), list(l) {}

        template<class TokenType = Token>
        terark_forceinline TokenType* get_token() {
            if (terark_likely(writer != nullptr)) {
                TERARK_ASSERT_NE(dynamic_cast<TokenType*>(writer), nullptr);
                return static_cast<TokenType*>(writer);
            }
            auto* tok = new TokenType();
            tok->m_tls = this;
            writer = tok;
            return tok;
        }

        void assert_current_thread() const {
            if (writer != nullptr) {
                TERARK_ASSERT_EQ(writer->m_thread_id, ThisThreadID());
            }
        }

        terark_forceinline Splice* peek_splice() const { return splice_hint; }

        // One heap splice per TLS. FinishHint only resets height.
        terark_forceinline Splice* get_splice() {
            if (splice_hint == nullptr) {
                splice_hint = list->AllocateSpliceOnHeap();
            }
            return splice_hint;
        }

        ~LazyFreeListTLS() override {
            if (writer != nullptr) {
                writer->dispose();
                writer = nullptr;
            }
            if (splice_hint != nullptr) {
                delete[] reinterpret_cast<char*>(splice_hint);
                splice_hint = nullptr;
            }
        }

        void clean_for_reuse() override {
            ReleaseIdleTokens();
            TCMemPoolOneThread<AlignSize>::clean_for_reuse();
        }

        void init_for_reuse() override {
            const size_t tid = ThisThreadID();
            if (writer) {
                writer->m_thread_id = tid;
            }
            TCMemPoolOneThread<AlignSize>::init_for_reuse();
        }

        void ReleaseIdleTokens() {
            auto rel = [](Token* t) {
                if (!t) {
                    return;
                }
                if (t->m_flags.state == AcquireIdle) {
                    t->release();
                }
                TERARK_VERIFY_EQ(t->m_flags.state, ReleaseDone);
                t->m_thread_id = size_t(-1);
            };
            rel(writer);
        }

        void ReleaseTokensForDestroy() {
            auto rel = [](Token* t) {
                if (!t) {
                    return;
                }
                if (t->m_flags.state == AcquireIdle || t->m_flags.state == AcquireDone) {
                    t->release();
                }
                TERARK_VERIFY_EQ(t->m_flags.state, ReleaseDone);
            };
            rel(writer);
        }

      private:
        Token* writer = nullptr;
        OffsetSkipList* list = nullptr;
        size_t mem_size = 0;
        std::deque<LazyFreeItem> q;
        Splice* splice_hint = nullptr;
    };

    void InitSplice() {
        m_seq_splice.height = 0;
        m_seq_splice.prev = m_seq_prev;
        m_seq_splice.next = m_seq_next;
    }

    void InitHead() {
        Node* h = AllocateNode(0, m_max_height_limit, tls_get());
        TERARK_VERIFY_F(h != nullptr, "OffsetSkipList head alloc failed, cap=%zd",
                        m_mem.capacity());
        m_head_loc = LocOf(base(), h);
        for (int i = 0; i < m_max_height_limit; ++i) {
            h->SetNextLoc(i, nil);
        }
    }

    void InitDummy() {
        m_dummy.m_list = this;
        m_dummy.m_prev = m_dummy.m_next = &m_dummy;
        m_dummy.m_verseq = m_dummy.m_min_verseq = 1;
        m_dummy.m_flags = {AcquireIdle, 0};
        m_dummy.m_tls = nullptr;
        m_dummy.m_thread_id = size_t(-1);
        m_token_qlen = 0;
    }

    void InstallMempoolTls() {
        m_mem.m_new_tc = [this](ThreadCacheMemPool<AlignSize>*)
            -> TCMemPoolOneThread<AlignSize>* { return new LazyFreeListTLS(this); };
    }

    void CloseFileMmap() {
        if (m_mmap_fd < 0) {
            return;
        }
        byte_t* p = const_cast<byte_t*>(m_mem.data());
        size_t n = m_mmap_size;
        intptr_t fd = m_mmap_fd;
        m_mmap_fd = -1;
        m_mmap_size = 0;
        m_mem.risk_release_ownership();
        if (p) {
            mmap_close(p, n, fd);
        }
    }

    void SyncNumNodes() {
        for_each_tls_token([this](Token* t) {
            if (t->m_num_nodes) {
                as_atomic(m_num_nodes).fetch_add(t->m_num_nodes,
                                                std::memory_order_relaxed);
                t->m_num_nodes = 0;
            }
        });
    }

    void DrainAllLazyFree() {
        m_mem.for_each_tls([this](TCMemPoolOneThread<AlignSize>* tc) {
            DrainList(*static_cast<LazyFreeListTLS*>(tc));
        });
    }

    void DrainList(LazyFreeListTLS& lzf) {
        while (!lzf.q.empty()) {
            const LazyFreeItem& x = lzf.q.front();
            m_mem.sfree(x.pos, x.len, &lzf);
            lzf.q.pop_front();
        }
        lzf.mem_size = 0;
    }

    void ReleaseTlsTokensForDestroy() {
        m_mem.for_each_tls([](TCMemPoolOneThread<AlignSize>* tc) {
            static_cast<LazyFreeListTLS*>(tc)->ReleaseTokensForDestroy();
        });
    }

    size_t RevokeExpired(LazyFreeListTLS& lzf, uint64_t min_verseq, bool all) {
        size_t n = all ? lzf.q.size() : std::min(lzf.q.size(), size_t(8));
        size_t revoked = 0;
        for (size_t i = 0; i < n; ++i) {
            const LazyFreeItem& head = lzf.q.front();
            if (head.age < min_verseq) {
                m_mem.sfree(head.pos, head.len, &lzf);
                revoked += head.len;
                lzf.q.pop_front();
            } else {
                break;
            }
        }
        lzf.mem_size -= revoked;
        return revoked;
    }
};
}// namespace terark
