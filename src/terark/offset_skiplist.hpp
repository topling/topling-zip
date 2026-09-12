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
        if (n == nullptr) {
            return nil;
        }
        auto off = reinterpret_cast<const byte_t*>(n) - base;
        TERARK_ASSERT_AL(off, AlignSize);
        return link_t(size_t(off) / AlignSize);
    }
    static Node* NodeAt(byte_t* base, link_t loc) {
        if (loc == nil) {
            return nullptr;
        }
        return reinterpret_cast<Node*>(base + size_t(loc) * AlignSize);
    }

    Node* Next(int n, byte_t* base) {
        TERARK_ASSERT_GE(n, 0);
        return NodeAt(base, (&m_next[0] - n)->load(std::memory_order_acquire));
    }
    void SetNext(int n, Node* x, byte_t* base) {
        TERARK_ASSERT_GE(n, 0);
        (&m_next[0] - n)->store(LocOf(base, x), std::memory_order_release);
    }
    bool CASNext(int n, Node* expected, Node* x, byte_t* base) {
        TERARK_ASSERT_GE(n, 0);
        link_t exp = LocOf(base, expected);
        return (&m_next[0] - n)->compare_exchange_strong(exp, LocOf(base, x));
    }
    Node* NoBarrier_Next(int n, byte_t* base) {
        TERARK_ASSERT_GE(n, 0);
        return NodeAt(base, (&m_next[0] - n)->load(std::memory_order_relaxed));
    }
    void NoBarrier_SetNext(int n, Node* x, byte_t* base) {
        TERARK_ASSERT_GE(n, 0);
        (&m_next[0] - n)->store(LocOf(base, x), std::memory_order_relaxed);
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

    explicit OffsetSkipList(Comparator cmp, size_t mem_cap,
                            int max_height = 14,
                            int branching_factor = 4)
        : m_max_height_limit(static_cast<uint16_t>(max_height)),
          m_branching(static_cast<uint16_t>(branching_factor)),
          m_scaled_inverse_branching(2147483648u / m_branching),
          m_mem(64),
          m_compare(cmp),
          m_max_height(1),
          m_head(nullptr) {
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
          m_head(nullptr) {
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
        m_head = NodeAt(base(), head_loc);
        TERARK_VERIFY(m_head != nullptr);
        m_num_nodes.store(num_nodes, std::memory_order_relaxed);
        m_readonly = true;
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
          m_head(nullptr) {
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


    link_t head_loc() const { return LocOf(base(), m_head); }
    int max_height() const { return GetMaxHeight(); }
    int k_max_height() const { return m_max_height_limit; }
    int k_branching() const { return m_branching; }

    // Optional leading_len sits at the allocation start (varint+value), then
    // [higher nexts][Node][key]. Dup-fail sfree can drop the contiguous top.
    char* AllocateKey(size_t key_size, size_t leading_len = 0) {
        Node* x = AllocateNode(key_size, RandomHeight(), leading_len);
        if (terark_unlikely(x == nullptr)) {
            return nullptr;
        }
        return const_cast<char*>(x->Key());
    }

    // Insert() returned an existing key: this node was never linked. Height
    // is still stashed in m_next[0]. key_size / leading_len must match
    // AllocateKey().
    void FreeUnusedKey(const char* key, size_t key_size, size_t leading_len = 0) {
        const size_t pos = KeyAllocPos(key, leading_len);
        Node* x = reinterpret_cast<Node*>(const_cast<char*>(key)) - 1;
        const int height = x->UnstashHeight();
        const size_t prefix = sizeof(link_t) * size_t(height - 1);
        m_mem.sfree(pos, leading_len + prefix + sizeof(Node) + key_size);
    }

    // Keep the leading_len bytes, sfree Node+key (the top). Returns
    // leading pos (same as KeyAllocPos).
    size_t FreeUnusedKeyKeepLeading(const char* key, size_t key_size,
                                    size_t leading_len) {
        TERARK_ASSERT_GT(leading_len, 0);
        TERARK_ASSERT_AL(leading_len, AlignSize);
        const size_t pos = KeyAllocPos(key, leading_len);
        Node* x = reinterpret_cast<Node*>(const_cast<char*>(key)) - 1;
        const int height = x->UnstashHeight();
        const size_t prefix = sizeof(link_t) * size_t(height - 1);
        const size_t drop = prefix + sizeof(Node) + key_size;
        TERARK_ASSERT_GE(drop, sizeof(link_t));
        m_mem.sfree(pos + leading_len, drop);
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
            if (!list->m_readonly || list->m_token_qlen) {
                if (terark_unlikely(m_tls == nullptr) && !list->m_readonly) {
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
            if (!m_list->m_readonly || m_list->m_token_qlen) {
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
            if (!m_list->m_readonly) {
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
            if (!m_list->m_readonly) {
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

        Splice* m_splice_hint = nullptr;

      protected:
        virtual ~Token() {
            if (m_splice_hint != nullptr) {
                delete[] reinterpret_cast<char*>(m_splice_hint);
                m_splice_hint = nullptr;
            }
            // Dummy is a queue sentinel (InitDummy sets AcquireIdle). Ctor
            // unwind never reaches ~OffsetSkipList, which is the only place
            // that would set ReleaseDone on m_dummy.
            if (m_list != nullptr && this == &m_list->m_dummy) {
                return;
            }
            TERARK_VERIFY_EQ(m_flags.state, ReleaseDone);
        }

        OffsetSkipList* skiplist() const { return m_list; }

      private:
        friend class OffsetSkipList;
        OffsetSkipList* m_list = nullptr;
        void* m_tls = nullptr;
        size_t m_thread_id = size_t(-1);
        // frequently sync with other threads
        Token* m_prev = nullptr;
        Token* m_next = nullptr;
        uint64_t m_verseq = 0;
        uint64_t m_min_verseq = 0;
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

    template<class TokenType = Token>
    TokenType* tls_token_nn() const {
        auto* mp = const_cast<ThreadCacheMemPool<AlignSize>*>(&m_mem);
        auto* lzf = static_cast<LazyFreeListTLS*>(mp->get_tls());
        if (lzf->writer == nullptr) {
            lzf->writer = new TokenType();
        } else {
            TERARK_ASSERT_NE(dynamic_cast<TokenType*>(lzf->writer), nullptr);
        }
        return static_cast<TokenType*>(lzf->writer);
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

    uint64_t num_nodes() const {
        return m_num_nodes.load(std::memory_order_relaxed);
    }
    size_t lazy_free_bytes() const {
        size_t n = 0;
        const_cast<ThreadCacheMemPool<AlignSize>&>(m_mem).for_each_tls(
            [&n](TCMemPoolOneThread<AlignSize>* tc) {
                n += static_cast<LazyFreeListTLS*>(tc)->mem_size;
            });
        return n;
    }


    // nullptr: inserted. Non-null: already present, that node's key.
    const char* Insert(const char* key, Token* token) {
        TERARK_ASSERT_EQ(token->state(), AcquireDone);
        return Insert<false>(key, &m_seq_splice, false);
    }

    const char* InsertWithHint(const char* key, Token* token) {
        TERARK_ASSERT_EQ(token->state(), AcquireDone);
        Splice* hint = token->m_splice_hint;
        if (terark_unlikely(hint == nullptr)) {
            hint = token->m_splice_hint = AllocateSpliceOnHeap();
        }
        return Insert<false>(key, hint, true);
    }

    // Reset height. Splice stays on the token until ~Token.
    void FinishHint(Token* token) {
        TERARK_ASSERT_NE(token->m_splice_hint, nullptr);
        token->m_splice_hint->height = 0;
    }

    const char* InsertConcurrently(const char* key, Token* token) {
        TERARK_ASSERT_EQ(token->state(), AcquireDone);
        link_t prev[kMaxPossibleHeight + 1];
        link_t next[kMaxPossibleHeight + 1];
        Splice splice;
        splice.prev = prev;
        splice.next = next;
        return Insert<true>(key, &splice, false);
    }

    const char* InsertWithHintConcurrently(const char* key, Token* token) {
        TERARK_ASSERT_EQ(token->state(), AcquireDone);
        Splice* hint = token->m_splice_hint;
        if (terark_unlikely(hint == nullptr)) {
            hint = token->m_splice_hint = AllocateSpliceOnHeap();
        }
        return Insert<true>(key, hint, true);
    }

  private:
    template<bool UseCAS>
    const char* Insert(const char* key, Splice* splice,
                       bool allow_partial_splice_fix) {
        Node* x = reinterpret_cast<Node*>(const_cast<char*>(key)) - 1;
        const DecodedKey key_decoded = m_compare.decode_key(key);
        int height = x->UnstashHeight();
        TERARK_ASSERT_BE(height, 1, m_max_height_limit);
        int max_height = GrowMaxHeight(height);
        TERARK_ASSERT_LE(max_height, kMaxPossibleHeight);
        (void) max_height;
        PrepareSplice(key_decoded, splice, allow_partial_splice_fix);
        const char* exist =
            LinkFromSplice<UseCAS>(x, key_decoded, height, splice);
        if (exist == nullptr) {
            m_num_nodes.fetch_add(1, std::memory_order_relaxed);
        }
        return exist;
    }

  public:
    // nullptr: absent. Non-null: that node's key (equal).
    const char* Get(const char* key, Token* token) const {
        TERARK_ASSERT_EQ(token->state(), AcquireDone);
        auto found = FindGreaterOrEqual(key, token);
        if (found.first != nullptr && found.second == 0) {
            return found.first->Key();
        }
        return nullptr;
    }

    bool Contains(const char* key, Token* token) const {
        return Get(key, token) != nullptr;
    }

    uint64_t EstimateCount(const char* key, Token* token) const {
        TERARK_ASSERT_EQ(token->state(), AcquireDone);
        return EstimateCount(key);
    }
    uint64_t EstimateCount(const char* key) const {
        uint64_t count = 0;
        byte_t* b = base();
        Node* x = m_head;
        int level = GetMaxHeight() - 1;
        const DecodedKey key_decoded = m_compare.decode_key(key);
        while (true) {
            if (x != m_head) {
                TERARK_ASSERT_LT(m_compare(x->Key(), key_decoded), 0);
            }
            Node* next = x->Next(level, b);
            if (next != nullptr) {
                __builtin_prefetch(static_cast<const void*>(next->Next(level, b)), 0, 1);
            }
            if (next == nullptr || m_compare(next->Key(), key_decoded) >= 0) {
                if (level == 0) {
                    return count;
                } else {
                    count *= m_branching;
                    level--;
                }
            } else {
                x = next;
                count++;
            }
        }
    }

    // second is this hop's compare, or +1 when next is nullptr / last_bigger
    // (already known greater; not the raw Comparator result).
    std::pair<Node*, int> FindGreaterOrEqual(const char* key, Token* token) const {
        TERARK_ASSERT_EQ(token->state(), AcquireDone);
        return FindGreaterOrEqual(key);
    }
    std::pair<Node*, int> FindGreaterOrEqual(const char* key) const {
        byte_t* b = base();
        Node* x = m_head;
        int level = GetMaxHeight() - 1;
        Node* last_bigger = nullptr;
        const DecodedKey key_decoded = m_compare.decode_key(key);
        while (true) {
            Node* next = x->Next(level, b);
            if (next != nullptr) {
                __builtin_prefetch(static_cast<const void*>(next->Next(level, b)), 0, 1);
            }
            if (x != m_head) {
                if (next != nullptr) {
                    TERARK_ASSERT_LT(m_compare(x->Key(), next->Key()), 0);
                }
                TERARK_ASSERT_LT(m_compare(x->Key(), key_decoded), 0);
            }
            int cmp = (next == nullptr || next == last_bigger)
                          ? 1
                          : m_compare(next->Key(), key_decoded);
            if (terark_unlikely(cmp == 0 || (cmp > 0 && level == 0))) {
                return {next, cmp};
            } else if (cmp < 0) {
                x = next;
            } else {
                last_bigger = next;
                level--;
            }
        }
    }

    void TEST_Validate() const {
        byte_t* b = base();
        Node* nodes[kMaxPossibleHeight];
        int max_height = GetMaxHeight();
        TERARK_ASSERT_GT(max_height, 0);
        for (int i = 0; i < max_height; i++) {
            nodes[i] = m_head;
        }
        while (nodes[0] != nullptr) {
            Node* l0_next = nodes[0]->Next(0, b);
            if (l0_next == nullptr) {
                break;
            }
            if (nodes[0] != m_head) {
                TERARK_ASSERT_LT(m_compare(nodes[0]->Key(), l0_next->Key()), 0);
            }
            nodes[0] = l0_next;
            int i = 1;
            while (i < max_height) {
                Node* next = nodes[i]->Next(i, b);
                if (next == nullptr) {
                    break;
                }
                if (m_compare.equal(nodes[0]->Key(), next->Key())) {
                    TERARK_ASSERT_EQ(next, nodes[0]);
                    nodes[i] = next;
                } else {
                    TERARK_ASSERT_LT(m_compare(nodes[0]->Key(), next->Key()), 0);
                    break;
                }
                i++;
            }
        }
        for (int i = 1; i < max_height; i++) {
            TERARK_ASSERT_NE(nodes[i], nullptr);
            TERARK_ASSERT_EQ(nodes[i]->Next(i, b), nullptr);
        }
    }

    void TEST_AllocHint(Token* token) {
        if (token->m_splice_hint == nullptr) {
            token->m_splice_hint = AllocateSpliceOnHeap();
        }
    }

    char* TEST_AllocateKeyWithHeight(size_t key_size, int height,
                                     size_t leading_len = 0) {
        TERARK_VERIFY_GE(height, 1);
        TERARK_VERIFY_LE(height, m_max_height_limit);
        Node* x = AllocateNode(key_size, height, leading_len);
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
    const char* TEST_InsertSkipPrepare(const char* key, Splice* splice) {
        TERARK_VERIFY(splice != nullptr);
        Node* x = reinterpret_cast<Node*>(const_cast<char*>(key)) - 1;
        const DecodedKey key_decoded = m_compare.decode_key(key);
        int height = x->UnstashHeight();
        GrowMaxHeight(height);
        const char* exist =
            LinkFromSplice<false>(x, key_decoded, height, splice);
        if (exist == nullptr) {
            m_num_nodes.fetch_add(1, std::memory_order_relaxed);
        }
        return exist;
    }

    // Pre-fix FindSpliceForLevel: stop when next == after even if after < key.
    void TEST_FindSpliceForLevelLegacy(const char* key, link_t before_loc,
                                       link_t after_loc, int level,
                                       link_t* out_prev, link_t* out_next) {
        const DecodedKey decoded = m_compare.decode_key(key);
        byte_t* b = base();
        Node* before = NodeAt(b, before_loc);
        Node* after = NodeAt(b, after_loc);
        while (true) {
            Node* next = before->Next(level, b);
            if (next == after || !KeyIsAfterNode(decoded, next)) {
                *out_prev = LocOf(b, before);
                *out_next = LocOf(b, next);
                return;
            }
            before = next;
        }
    }

    // Pre-fix LinkFromSplice (non-CAS, skip Prepare): treat splice locs as
    // stable Node*. Only next-equal is duplicate; prev > key / next < key
    // are asserted then linked anyway (release) or abort (debug).
    const char* TEST_InsertSkipPrepareLegacy(const char* key, Splice* splice) {
        TERARK_VERIFY(splice != nullptr);
        Node* x = reinterpret_cast<Node*>(const_cast<char*>(key)) - 1;
        const DecodedKey key_decoded = m_compare.decode_key(key);
        int height = x->UnstashHeight();
        GrowMaxHeight(height);
        byte_t* b = base();
        for (int i = 0; i < height; ++i) {
            Node* prev = NodeAt(b, splice->prev[i]);
            Node* next = NodeAt(b, splice->next[i]);
            if (i == 0 && next != nullptr &&
                m_compare.equal(next->Key(), key_decoded)) {
                x->StashHeight(height);
                return next->Key();
            }
            x->NoBarrier_SetNext(i, next, b);
            prev->SetNext(i, x, b);
        }
        m_num_nodes.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }


    size_t mem_size() const { return m_mem.size(); }
    size_t mem_capacity() const { return m_mem.capacity(); }
    size_t mem_align_size() const { return AlignSize; }
    const byte_t* mem_data() const { return m_mem.data(); }
    ThreadCacheMemPool<AlignSize>& mempool() { return m_mem; }
    const ThreadCacheMemPool<AlignSize>& mempool() const { return m_mem; }

    intptr_t mmap_fd() const { return m_mmap_fd; }
    const std::string& mmap_fpath() const { return m_mmap_fpath; }
    bool is_mmap() const { return m_mmap_fd >= 0; }
    bool is_readonly() const { return m_readonly; }
    uint32_t token_qlen() const { return m_token_qlen; }
    fstring get_mmap() const {
        return is_mmap() ? fstring(reinterpret_cast<const char*>(m_mem.data()),
                                   m_mem.capacity())
                         : fstring();
    }
    // File mmap: drop unused tail pages and ftruncate to used size
    // (Patricia mempool_set_readonly, MWMR only).
    void set_readonly() {
        if (m_readonly) {
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
        m_readonly = true;
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

    template<bool ReadOnly>
    class IteratorTpl : public std::conditional_t<ReadOnly, IteratorReadonlyBase, IteratorWritableBase> {
        static constexpr bool NeedToken = !ReadOnly;
      public:
        IteratorTpl(const IteratorTpl&) = delete;
        IteratorTpl& operator=(const IteratorTpl&) = delete;

        explicit IteratorTpl(const OffsetSkipList* list) {
            TERARK_ASSERT_NE(list, nullptr);
            if constexpr (ReadOnly) {
                TERARK_VERIFY(list->is_readonly());
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

        bool Valid() const { return m_node != nullptr; }
        const char* key() const {
            TERARK_ASSERT_NE(m_node, nullptr);
            return m_node->Key();
        }

        void Next() {
            TERARK_ASSERT_NE(m_node, nullptr);
            if constexpr (NeedToken) {
                TERARK_ASSERT_EQ(this->state(), AcquireDone);
            }
            m_node = m_node->Next(0, this->skiplist()->base());
            if constexpr (NeedToken) {
                this->MaybeUpdateTokenOnScan();
            }
        }

        void Prev() {
            TERARK_ASSERT_NE(m_node, nullptr);
            m_node = this->skiplist()->FindLessThan(m_node->Key());
            if (m_node == this->skiplist()->m_head) {
                m_node = nullptr;
            }
            if constexpr (NeedToken) {
                this->MaybeUpdateTokenOnScan();
            }
        }

        void Seek(const char* target) {
            if constexpr (NeedToken) {
                this->UpdateToken();
            }
            m_node = this->skiplist()->FindGreaterOrEqual(target).first;
        }

        void SeekForPrev(const char* target) {
            Seek(target);
            if (!Valid()) {
                SeekToLast();
            }
            while (Valid() && this->skiplist()->LessThan(target, key())) {
                Prev();
            }
        }

        void RandomSeek() {
            if constexpr (NeedToken) {
                this->UpdateToken();
            }
            m_node = this->skiplist()->FindRandomEntry();
        }

        void SeekToFirst() {
            if constexpr (NeedToken) {
                this->UpdateToken();
            }
            m_node = this->skiplist()->m_head->Next(0, this->skiplist()->base());
        }

        void SeekToLast() {
            if constexpr (NeedToken) {
                this->UpdateToken();
            }
            m_node = this->skiplist()->FindLast();
            if (m_node == this->skiplist()->m_head) {
                m_node = nullptr;
            }
        }

        void ResetKey(const char* key) {
            if constexpr (NeedToken) {
                TERARK_ASSERT_EQ(this->state(), AcquireDone);
            }
            m_node = key ? reinterpret_cast<Node*>(const_cast<char*>(key)) - 1
                         : nullptr;
        }

      private:
        Node* m_node = nullptr;
    };
    using Iterator = IteratorTpl<false>;
    using ReadonlyIterator = IteratorTpl<true>;

  private:
    static link_t LocOf(const byte_t* base, const Node* n) {
        if (n == nullptr) {
            return nil;
        }
        auto off = reinterpret_cast<const byte_t*>(n) - base;
        TERARK_ASSERT_AL(off, AlignSize);
        return link_t(size_t(off) / AlignSize);
    }
    static Node* NodeAt(byte_t* base, link_t loc) {
        if (loc == nil) {
            return nullptr;
        }
        return reinterpret_cast<Node*>(base + size_t(loc) * AlignSize);
    }

    byte_t* base() const { return const_cast<byte_t*>(m_mem.data()); }

    int GetMaxHeight() const {
        return m_max_height.load(std::memory_order_relaxed);
    }

    static uint32_t NextRand() {
        thread_local uint32_t seed = 1;
        seed = static_cast<uint32_t>((uint64_t(seed) * 16807u) % 2147483647u);
        return seed;
    }

    int RandomHeight() {
        int height = 1;
        while (height < m_max_height_limit && height < kMaxPossibleHeight &&
               NextRand() < m_scaled_inverse_branching) {
            height++;
        }
        TERARK_ASSERT_GT(height, 0);
        TERARK_ASSERT_LE(height, m_max_height_limit);
        return height;
    }


    Node* AllocateNode(size_t key_size, int height, size_t leading_len = 0) {
        TERARK_ASSERT_AL(leading_len, AlignSize);
        const size_t prefix = sizeof(link_t) * size_t(height - 1);
        const size_t nbytes = leading_len + prefix + sizeof(Node) + key_size;
        size_t pos = m_mem.alloc(nbytes);
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

    bool Equal(const char* a, const char* b) const {
        return m_compare.equal(a, b);
    }
    bool LessThan(const char* a, const char* b) const {
        return m_compare(a, b) < 0;
    }

    bool KeyIsAfterNode(const char* key, Node* n) const {
        TERARK_ASSERT_NE(n, m_head);
        return (n != nullptr) && (m_compare(n->Key(), key) < 0);
    }

    bool KeyIsAfterNode(const DecodedKey& key, Node* n) const {
        TERARK_ASSERT_NE(n, m_head);
        return (n != nullptr) && (m_compare(n->Key(), key) < 0);
    }


    Node* FindLessThan(const char* key, Node** prev = nullptr) const {
        return FindLessThan(key, prev, m_head, GetMaxHeight(), 0);
    }

    Node* FindLessThan(const char* key, Node** prev, Node* root,
                       int top_level, int bottom_level) const {
        TERARK_ASSERT_GT(top_level, bottom_level);
        byte_t* b = base();
        int level = top_level - 1;
        Node* x = root;
        Node* last_not_after = nullptr;
        const DecodedKey key_decoded = m_compare.decode_key(key);
        while (true) {
            TERARK_ASSERT_NE(x, nullptr);
            Node* next = x->Next(level, b);
            if (next != nullptr) {
                __builtin_prefetch(static_cast<const void*>(next->Next(level, b)), 0, 1);
            }
            if (x != m_head) {
                if (next != nullptr) {
                    TERARK_ASSERT_LT(m_compare(x->Key(), next->Key()), 0);
                }
                TERARK_ASSERT_LT(m_compare(x->Key(), key_decoded), 0);
            }
            if (next != last_not_after && KeyIsAfterNode(key_decoded, next)) {
                TERARK_ASSERT_NE(next, nullptr);
                x = next;
            } else {
                if (prev != nullptr) {
                    prev[level] = x;
                }
                if (level == bottom_level) {
                    return x;
                } else {
                    last_not_after = next;
                    level--;
                }
            }
        }
    }

    Node* FindLast() const {
        byte_t* b = base();
        Node* x = m_head;
        int level = GetMaxHeight() - 1;
        while (true) {
            Node* next = x->Next(level, b);
            if (next == nullptr) {
                if (level == 0) {
                    return x;
                } else {
                    level--;
                }
            } else {
                x = next;
            }
        }
    }

    Node* FindRandomEntry() const {
        byte_t* b = base();
        Node* x = m_head;
        Node* scan_node = nullptr;
        Node* limit_node = nullptr;
        std::vector<Node*> lvl_nodes;
        int level = GetMaxHeight() - 1;
        while (level >= 0) {
            lvl_nodes.clear();
            scan_node = x;
            while (scan_node != limit_node) {
                lvl_nodes.push_back(scan_node);
                scan_node = scan_node->Next(level, b);
            }
            uint32_t rnd_idx = NextRand() % static_cast<uint32_t>(lvl_nodes.size());
            x = lvl_nodes[rnd_idx];
            if (rnd_idx + 1 < lvl_nodes.size()) {
                limit_node = lvl_nodes[rnd_idx + 1];
            }
            level--;
        }
        return x == m_head && m_head != nullptr ? m_head->Next(0, b) : x;
    }


    template<bool prefetch_before>
    void FindSpliceForLevel(const DecodedKey& key, link_t before_loc,
                            link_t after_loc, int level, link_t* out_prev,
                            link_t* out_next) {
        byte_t* b = base();
        Node* before = NodeAt(b, before_loc);
        Node* after = NodeAt(b, after_loc);
        // prev/next are locs: the node at before_loc may no longer be a
        // predecessor (hint reuse, CAS retry). after_loc may be < key.
        if (before == nullptr ||
            (before != m_head && !KeyIsAfterNode(key, before))) {
            before = m_head;
            after = nullptr;
        }
        while (true) {
            Node* next = before->Next(level, b);
            if (next != nullptr) {
                __builtin_prefetch(static_cast<const void*>(next->Next(level, b)), 0, 1);
            }
            if (prefetch_before == true) {
                if (next != nullptr && level > 0) {
                    __builtin_prefetch(static_cast<const void*>(next->Next(level - 1, b)),
                                       0, 1);
                }
            }
            if (before != m_head) {
                if (next != nullptr) {
                    TERARK_ASSERT_LT(m_compare(before->Key(), next->Key()), 0);
                }
                TERARK_ASSERT_LT(m_compare(before->Key(), key), 0);
            }
            if (!KeyIsAfterNode(key, next)) {
                *out_prev = LocOf(b, before);
                *out_next = LocOf(b, next);
                return;
            }
            if (next == after) {
                after = nullptr;
            }
            before = next;
        }
    }

    void RecomputeSpliceLevels(const DecodedKey& key, Splice* splice,
                               int recompute_level) {
        TERARK_ASSERT_GT(recompute_level, 0);
        TERARK_ASSERT_LE(recompute_level, splice->height);
        for (int i = recompute_level - 1; i >= 0; --i) {
            FindSpliceForLevel<true>(key, splice->prev[i + 1], splice->next[i + 1], i,
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

    void PrepareSplice(const DecodedKey& key, Splice* splice,
                       bool allow_partial_splice_fix) {
        byte_t* b = base();
        const link_t head = LocOf(b, m_head);
        int max_height = m_max_height.load(std::memory_order_relaxed);
        int recompute_height = 0;
        if (splice->height < max_height) {
            splice->prev[max_height] = head;
            splice->next[max_height] = nil;
            splice->height = max_height;
            recompute_height = max_height;
        } else {
            while (recompute_height < max_height) {
                Node* prev = NodeAt(b, splice->prev[recompute_height]);
                Node* next = NodeAt(b, splice->next[recompute_height]);
                if (LocOf(b, prev->Next(recompute_height, b)) !=
                    splice->next[recompute_height]) {
                    ++recompute_height;
                } else if (prev != m_head && !KeyIsAfterNode(key, prev)) {
                    if (allow_partial_splice_fix) {
                        link_t bad = splice->prev[recompute_height];
                        while (splice->prev[recompute_height] == bad) {
                            ++recompute_height;
                        }
                    } else {
                        recompute_height = max_height;
                    }
                } else if (KeyIsAfterNode(key, next)) {
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
    }

    template<bool UseCAS>
    const char* LinkFromSplice(Node* x, const DecodedKey& key, int height,
                               Splice* splice) {
        byte_t* b = base();
        const link_t head = LocOf(b, m_head);
        bool splice_is_valid = true;
        auto fail_duplicate = [&](Node* found) {
            // CAS retry may have overwritten m_next[0] via NoBarrier_SetNext.
            // Restore stash so FreeUnusedKey can recover the allocation.
            x->StashHeight(height);
            return found->Key();
        };
        if (UseCAS) {
            for (int i = 0; i < height; ++i) {
                while (true) {
                    Node* next = NodeAt(b, splice->next[i]);
                    Node* prev = NodeAt(b, splice->prev[i]);
                    if (terark_unlikely(i == 0 && next != nullptr &&
                                        m_compare.equal(next->Key(), key))) {
                        return fail_duplicate(next);
                    }
                    if (terark_unlikely(i == 0 && prev != m_head &&
                                        m_compare.equal(prev->Key(), key))) {
                        return fail_duplicate(prev);
                    }
                    if ((next != nullptr && m_compare(next->Key(), key) < 0) ||
                        (prev != m_head && m_compare(prev->Key(), key) > 0)) {
                        FindSpliceForLevel<false>(key, head, nil, i, &splice->prev[i],
                                                  &splice->next[i]);
                        if (i > 0) {
                            splice_is_valid = false;
                        }
                        continue;
                    }
                    if (next != nullptr) {
                        TERARK_ASSERT_GT(m_compare(next->Key(), key), 0);
                    }
                    if (prev != m_head) {
                        TERARK_ASSERT_LT(m_compare(prev->Key(), key), 0);
                    }
                    x->NoBarrier_SetNext(i, next, b);
                    if (prev->CASNext(i, next, x, b)) {
                        break;
                    }
                    FindSpliceForLevel<false>(key, splice->prev[i], nil, i,
                                              &splice->prev[i], &splice->next[i]);
                    if (i > 0) {
                        splice_is_valid = false;
                    }
                }
            }
        } else {
            for (int i = 0; i < height; ++i) {
                Node* prev = NodeAt(b, splice->prev[i]);
                Node* next = NodeAt(b, splice->next[i]);
                if (LocOf(b, prev->Next(i, b)) != splice->next[i]) {
                    FindSpliceForLevel<false>(key, splice->prev[i], nil, i,
                                              &splice->prev[i], &splice->next[i]);
                    prev = NodeAt(b, splice->prev[i]);
                    next = NodeAt(b, splice->next[i]);
                }
                if (terark_unlikely(i == 0 && next != nullptr &&
                                    m_compare.equal(next->Key(), key))) {
                    return fail_duplicate(next);
                }
                if (terark_unlikely(i == 0 && prev != m_head &&
                                    m_compare.equal(prev->Key(), key))) {
                    return fail_duplicate(prev);
                }
                if ((next != nullptr && m_compare(next->Key(), key) < 0) ||
                    (prev != m_head && m_compare(prev->Key(), key) > 0)) {
                    FindSpliceForLevel<false>(key, head, nil, i, &splice->prev[i],
                                              &splice->next[i]);
                    prev = NodeAt(b, splice->prev[i]);
                    next = NodeAt(b, splice->next[i]);
                    if (terark_unlikely(i == 0 && next != nullptr &&
                                        m_compare.equal(next->Key(), key))) {
                        return fail_duplicate(next);
                    }
                    if (terark_unlikely(i == 0 && prev != m_head &&
                                        m_compare.equal(prev->Key(), key))) {
                        return fail_duplicate(prev);
                    }
                }
                if (next != nullptr) {
                    TERARK_ASSERT_GT(m_compare(next->Key(), key), 0);
                }
                if (prev != m_head) {
                    TERARK_ASSERT_LT(m_compare(prev->Key(), key), 0);
                }
                TERARK_ASSERT_EQ(LocOf(b, prev->Next(i, b)), splice->next[i]);
                x->NoBarrier_SetNext(i, next, b);
                prev->SetNext(i, x, b);
            }
        }
        if (splice_is_valid) {
            const link_t xloc = LocOf(b, x);
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
    bool m_readonly = false;
    std::string m_mmap_fpath;

    // frequently updating
    mutable Token m_dummy;
    mutable uint32_t m_token_qlen = 0;
    std::atomic<int> m_max_height;
    mutable std::mutex m_token_mtx;
    Node* m_head;
    std::atomic<uint64_t> m_num_nodes{0};

    Splice m_seq_splice;
    link_t m_seq_prev[kMaxPossibleHeight + 1];
    link_t m_seq_next[kMaxPossibleHeight + 1];

    struct LazyFreeItem {
        uint64_t age;
        size_t pos;
        size_t len;
    };
    struct LazyFreeListTLS : TCMemPoolOneThread<AlignSize> {
        Token* writer = nullptr;
        OffsetSkipList* list = nullptr;
        size_t mem_size = 0;
        std::deque<LazyFreeItem> q;
        explicit LazyFreeListTLS(OffsetSkipList* l)
            : TCMemPoolOneThread<AlignSize>(&l->m_mem), list(l) {}

        ~LazyFreeListTLS() override {
            if (writer != nullptr) {
                writer->dispose();
                writer = nullptr;
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
    };

    void InitSplice() {
        m_seq_splice.height = 0;
        m_seq_splice.prev = m_seq_prev;
        m_seq_splice.next = m_seq_next;
    }

    void InitHead() {
        m_head = AllocateNode(0, m_max_height_limit);
        TERARK_VERIFY_F(m_head != nullptr, "OffsetSkipList head alloc failed, cap=%zd",
                        m_mem.capacity());
        byte_t* b = base();
        for (int i = 0; i < m_max_height_limit; ++i) {
            m_head->SetNext(i, nullptr, b);
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

    void DrainAllLazyFree() {
        m_mem.for_each_tls([this](TCMemPoolOneThread<AlignSize>* tc) {
            DrainList(*static_cast<LazyFreeListTLS*>(tc));
        });
    }

    void DrainList(LazyFreeListTLS& lzf) {
        while (!lzf.q.empty()) {
            const LazyFreeItem& x = lzf.q.front();
            m_mem.sfree(x.pos, x.len);
            lzf.q.pop_front();
        }
        lzf.mem_size = 0;
    }

    void ReleaseIdleTlsTokens() {
        m_mem.for_each_tls([](TCMemPoolOneThread<AlignSize>* tc) {
            static_cast<LazyFreeListTLS*>(tc)->ReleaseIdleTokens();
        });
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
                m_mem.sfree(head.pos, head.len);
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
