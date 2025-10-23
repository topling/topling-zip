#pragma once
#include "fsa.hpp"
#include <terark/util/enum.hpp>
#include <atomic>

// File hierarchy : cspptrie.hpp ├─> cspptrie.inl ├─> cspptrie.cpp

namespace terark {

template<size_t Align> class PatriciaMem;
class MainPatricia;

#define TERARK_friend_class_Patricia \
    friend class Patricia;   \
    friend class MainPatricia;   \
    friend class PatriciaMem<4>; \
    friend class PatriciaMem<8>

/*
 * Generated Patricia trie from matching deterministic finite automaton.
 *
 * Essentially, Patricia trie is one kind of specific radix tree which radix
 * aligns to power of 2. It may be 1, as branching in every single bit, or 8
 * which commonly interpreted as sigma of ASCII code, or very by definition.
 *
 * About Patricia trie :
 *     https://en.wikipedia.org/wiki/Radix_tree
 * About Automata theory :
 *     https://en.wikipedia.org/wiki/Automata_theory
 * About Deterministic finite automaton :
 *     https://en.wikipedia.org/wiki/Deterministic_finite_automaton
 */
class TERARK_DLL_EXPORT alignas(64) Patricia : public MatchingDFA {
    Patricia(const Patricia&) = delete;
    Patricia& operator=(const Patricia&) = delete;
    Patricia(Patricia&&) = delete;
    Patricia& operator=(Patricia&&) = delete;
public:
    TERARK_ENUM_PLAIN_INCLASS(ConcurrentLevel, byte_t,
        NoWriteReadOnly,     // 0
        SingleThreadStrict,  // 1
        SingleThreadShared,  // 2, iterator with token will keep valid
        OneWriteMultiRead,   // 3
        MultiWriteMultiRead  // 4
    );
    class TERARK_DLL_EXPORT TokenBase;
protected:
    TERARK_ENUM_PLAIN_INCLASS(TokenState, byte_t,
        ReleaseDone,
        AcquireDone,
        AcquireIdle // only this  thread  can set to AcquireIdle
    );
    struct TokenFlags {
        // state and is_head must be set simultaneously as atomic
        TokenState  state;
        byte_t      is_head;
    };
    static_assert(sizeof(TokenFlags) == 2, "sizeof(TokenFlags) == 2");
    static const size_t s_mempool_offset;
public:
    class TERARK_DLL_EXPORT TokenBase : protected boost::noncopyable {
        TERARK_friend_class_Patricia;
    protected:
        Patricia*     m_trie;
        size_t        m_valpos; // raw pos, not multiply by AlignSize
        void*         m_tls; // unused for ReaderToken
        size_t        m_thread_id;

        // frequently sync with other threads
        TokenBase*    m_prev;
        TokenBase*    m_next;
        ullong        m_verseq;
        ullong        m_min_verseq;
        TokenFlags    m_flags;

        void maybe_rotate(Patricia*, TokenState);
        void rotate(Patricia*, TokenState);
        void remove_self();
        void add_to_back(Patricia*);
        void mt_acquire(Patricia*);
        void mt_release(Patricia*);
        void init_tls(Patricia*) noexcept;
        //void reset_null();
        TokenBase();
        virtual ~TokenBase();
    public:
        virtual void idle();
        bool lookup(fstring, size_t root = initial_state);
        void acquire(Patricia*);
        void release();
        void dispose(); ///< delete lazy

        Patricia* trie() const { return m_trie; }
        bool has_value() const { return size_t(-1) != m_valpos; }
        size_t get_valpos() const { return m_valpos; }
        const void* value() const {
            assert(size_t(-1) != m_valpos);
            auto mp = (const valvec<byte_t>*)((byte_t*)m_trie + s_mempool_offset);
            return mp->data() + m_valpos;
        }
        template<class T>
        T value_of() const {
            assert(sizeof(T) == m_trie->m_valsize);
            assert(size_t(-1) != m_valpos);
            assert(m_valpos % m_trie->mem_align_size() == 0);
            if (sizeof(T) == 4)
              return   aligned_load<T>(value());
            else
              return unaligned_load<T>(value());
        }
        template<class T>
        T& mutable_value_of() const {
            assert(sizeof(T) == m_trie->m_valsize);
            assert(size_t(-1) != m_valpos);
            assert(m_valpos % m_trie->mem_align_size() == 0);
            return *(T*)(value());
        }
    };

    class TERARK_DLL_EXPORT ReaderToken : public TokenBase {
        TERARK_friend_class_Patricia;
    protected:
        virtual ~ReaderToken();
    public:
        ReaderToken();
    };
    using ReaderTokenPtr = std::unique_ptr<ReaderToken, DisposeAsDelete>;
    class TERARK_DLL_EXPORT SingleReaderToken : public TokenBase {
    public:
        explicit SingleReaderToken(Patricia* trie);
        ~SingleReaderToken() override;
    };

    class TERARK_DLL_EXPORT WriterToken : public TokenBase {
        TERARK_friend_class_Patricia;
    protected:
        virtual bool init_value(void* valptr, size_t valsize) noexcept;
        virtual void destroy_value(void* valptr, size_t valsize) noexcept;
        virtual ~WriterToken();
    public:
        WriterToken();
        bool insert(fstring key, void* value, size_t root = initial_state);
    };
    using WriterTokenPtr = std::unique_ptr<WriterToken, DisposeAsDelete>;
    class TERARK_DLL_EXPORT SingleWriterToken : public WriterToken {
    public:
        ~SingleWriterToken();
    };
    class TERARK_DLL_EXPORT Iterator : public ADFA_LexIterator, public ReaderToken {
    protected:
        Iterator(Patricia*);
        ~Iterator();
    public:
        void dispose() final;
        virtual void token_detach_iter() = 0;
    };
    using IteratorPtr = std::unique_ptr<Iterator, DisposeAsDelete>;

    struct MemStat {
        valvec<size_t> fastbin;
        size_t used_size;
        size_t capacity;
        size_t frag_size; // = fast + huge
        size_t huge_size;
        size_t huge_cnt;
        size_t lazy_free_sum;
        size_t lazy_free_cnt;
    };
    static Patricia* create(size_t valsize,
                            size_t maxMem = 512<<10,
                            ConcurrentLevel = OneWriteMultiRead);
    MemStat mem_get_stat() const;
    virtual size_t mem_align_size() const = 0;
    virtual size_t mem_frag_size() const = 0;
    virtual void mem_get_stat(MemStat*) const = 0;

    /// @returns
    ///  true: key does not exists
    ///     token->has_value() == false : reached memory limit
    ///     token->has_value() == true  : insert success,
    ///                              and value is copyed to token->value()
    ///  false: key has existed
    ///
    terark_forceinline
    bool insert(fstring key, void* value, WriterToken* token, size_t root = initial_state) {
      #if !TOPLING_USE_BOUND_PMF
        return (this->*m_insert)(key, value, token, root);
      #else
        return m_insert(this, key, value, token, root);
      #endif
    }

    ConcurrentLevel concurrent_level() const { return m_writing_concurrent_level; }
    virtual bool lookup(fstring key, TokenBase* token, size_t root = initial_state) const = 0;
    virtual void set_readonly() = 0;
    virtual bool  is_readonly() const = 0;
    virtual WriterTokenPtr& tls_writer_token() noexcept = 0;
    virtual ReaderToken* tls_reader_token() noexcept = 0;

    terark_forceinline
    WriterToken* tls_writer_token_nn() {
        return tls_writer_token_nn<WriterToken>();
    }
    /// '_nn' suffix means 'not null'
    template<class WriterTokenType>
    terark_forceinline
    WriterTokenType* tls_writer_token_nn() {
        // WriterToken can be used for read, m_writing_concurrent_level may
        // be set to NoWriteReadOnly after insert phase, it is valid to use
        // WriterToken in this case
        TERARK_ASSERT_F(m_mempool_concurrent_level >= OneWriteMultiRead, "%s",
                        enum_stdstr(m_mempool_concurrent_level).c_str());
        WriterTokenPtr& token = tls_writer_token();
        if (terark_likely(token.get() != NULL)) {
            assert(dynamic_cast<WriterTokenType*>(token.get()) != NULL);
        }
        else {
            token.reset(new WriterTokenType());
            token->init_tls(this);
        }
        return static_cast<WriterTokenType*>(token.get());
    }
    template<class NewFunc>
    terark_forceinline
    auto tls_writer_token_nn(NewFunc New) -> decltype(New()) {
        // WriterToken can be used for read, m_writing_concurrent_level may
        // be set to NoWriteReadOnly after insert phase, it is valid to use
        // WriterToken in this case
        TERARK_ASSERT_F(m_mempool_concurrent_level >= OneWriteMultiRead, "%s",
                        enum_stdstr(m_mempool_concurrent_level).c_str());
        typedef decltype(New()) PtrType;
        WriterTokenPtr& token = tls_writer_token();
        if (terark_likely(token.get() != NULL)) {
            assert(dynamic_cast<PtrType>(token.get()) != NULL);
        }
        else {
            token.reset(New());
            token->init_tls(this);
        }
        return static_cast<PtrType>(token.get());
    }

    virtual void for_each_tls_token(std::function<void(TokenBase*)>) = 0;

    Iterator* new_iter(size_t root = initial_state) const;
    size_t iter_mem_size(size_t root = initial_state) const;
    void cons_iter(void* mem, size_t root = initial_state) const;

    struct Stat {
        size_t n_fork;
        size_t n_split;
        size_t n_mark_final;
        size_t n_add_state_move;
        size_t sum() const { return n_fork + n_split + n_mark_final + n_add_state_move; }
    };
    size_t get_valsize() const { return m_valsize; }
    virtual const Stat& trie_stat() const = 0;
    virtual const Stat& sync_stat() = 0;
    virtual size_t num_words() const = 0;
    virtual void mempool_tc_populate(size_t) = 0;
    virtual size_t get_token_qlen() const noexcept = 0;
    virtual void print_mempool_stat(FILE*) const noexcept = 0;
    ~Patricia();
protected:
    Patricia();
    bool insert_readonly_throw(fstring key, void* value, WriterToken*, size_t root);
    typedef bool (Patricia::*insert_pmf_t)(fstring, void*, WriterToken*, size_t root);
#if !TOPLING_USE_BOUND_PMF
    typedef bool (Patricia::*insert_func_t)(fstring, void*, WriterToken*, size_t root);
#else
    typedef bool (*insert_func_t)(Patricia*, fstring, void*, WriterToken*, size_t root);
#endif
    insert_func_t    m_insert;
    ConcurrentLevel  m_writing_concurrent_level;
    ConcurrentLevel  m_mempool_concurrent_level;
    bool             m_is_virtual_alloc;
    uint32_t         m_valsize;
};

terark_forceinline
bool Patricia::TokenBase::lookup(fstring key, size_t root) {
    return m_trie->lookup(key, this, root);
}

terark_forceinline
bool Patricia::WriterToken::insert(fstring key, void* value, size_t root) {
    return m_trie->insert(key, value, this, root);
}

TERARK_DLL_EXPORT void CSPP_SetDebugLevel(long level);
TERARK_DLL_EXPORT long CSPP_GetDebugLevel();

} // namespace terark
