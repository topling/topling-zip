
#include "da_cache_fixed_strvec.hpp"
#include <terark/fsa/double_array_trie.hpp>
#include <terark/util/hugepage.hpp>
#include <terark/util/profiling.hpp>

namespace terark {

static bool g_useHugePage = getEnvBool("TerarkUseHugePage", true);
static bool g_showStat = getEnvBool("StrVecSearchCacheDFA_showStat");

class DaCacheFixedStrVec::MyAppendOnlyTrie {
public:
    struct state_t {
        uint32_t m_firstChild;
        uint08_t m_incomingLabel;
        uint08_t m_padding;
        uint16_t m_numChildren;
        uint32_t m_lo;
        uint32_t m_hi;
        state_t() {
            m_firstChild = UINT32_MAX;
            m_incomingLabel = 0;
            m_padding = 0;
            m_numChildren = 0;
            m_lo = UINT32_MAX;
            m_hi = UINT32_MAX;
        }
    };
    valvec<state_t> states;
    typedef uint32_t state_id_t;
    static const size_t   sigma = 256;
    static const uint32_t max_state = UINT32_MAX - 2;
    static const uint32_t nil_state = UINT32_MAX - 1;

    size_t total_states() const { return states.size(); }
    bool is_term(size_t) const { return false; }

    size_t get_all_move(size_t state, CharTarget<size_t>* moves) const {
        auto lstates = states.data();
        size_t beg = lstates[state].m_firstChild;
        size_t end = lstates[state].m_numChildren + beg;
        for (size_t i = beg; i < end; ++i) {
            moves[i-beg] = CharTarget<size_t>(lstates[i].m_incomingLabel, i);
        }
        return end - beg;
    }
    void add_all_move(size_t state, const CharTarget<size_t>* moves, size_t nMoves) {
        size_t firstChild = states.size() - nMoves;
        auto children = states.data() + firstChild;
        states[state].m_firstChild = firstChild;
        states[state].m_numChildren = uint16_t(nMoves);
        for (size_t i = 0; i < nMoves; ++i) {
            children[i].m_incomingLabel = moves[i].ch;
            assert(moves[i].target == firstChild + i);
        }
    }
    size_t new_state() {
        size_t oldsize = states.size();
        states.push_back();
        return oldsize;
    }
    void finish_append() {
        states.push_back();
        states.end()[-1].m_firstChild = states.end()[-2].m_firstChild;
    }
    void erase_all() {}
    void shrink_to_fit() { states.shrink_to_fit(); }
};

#pragma pack(push,1)
class DaCacheFixedStrVec::MyDoubleArrayNode {
public:
    typedef uint32_t state_id_t;
    static const state_id_t max_state = 0x0FFFFFFE;
    static const state_id_t nil_state = 0x0FFFFFFF;

    uint32_t m_child0; // aka base, the child state on transition: '\0'
    uint32_t m_parent; // aka check
    uint32_t m_lo;
    uint32_t m_hi;

    MyDoubleArrayNode() noexcept {
        m_lo = 0;
        m_hi = 0;
        m_child0 = nil_state;
        m_parent = nil_state;
    }
    void set_term_bit() {}
    void set_free_bit() { m_parent = nil_state; }
    void set_child0(state_id_t x) { m_child0 = x; }
    void set_parent(state_id_t x) {
        assert(x < max_state);
        // also clear free flag
        m_parent = x;
    }

    bool is_term() const noexcept { return true; }
    bool is_free() const noexcept { return nil_state == m_parent; }
    size_t child0() const noexcept { /*assert(!is_free());*/ return m_child0; }
    size_t parent() const noexcept { /*assert(!is_free());*/ return m_parent; }
};
#pragma pack(pop)

class DaCacheFixedStrVec::MyDoubleArrayTrie
    : public DoubleArrayTrie<DaCacheFixedStrVec::MyDoubleArrayNode>
{
    friend class DaCacheFixedStrVec;
    friend class HashStrVecSearchCacheDFA;
public:
    BOOST_STATIC_ASSERT(sizeof(DaCacheFixedStrVec::MyDoubleArrayNode) == 16);
    void useHugePage();
};

void DaCacheFixedStrVec::MyDoubleArrayTrie::useHugePage() {
#if defined(_MSC_VER)
#else
    use_hugepage_advise(&states);
#endif
}

DaCacheFixedStrVec::DaCacheFixedStrVec() {
    m_da_data = NULL;
    m_da_size = 0;
    m_da_free = 0;
}

DaCacheFixedStrVec::~DaCacheFixedStrVec() {
    auto da = (byte_t*)m_da_data;
    if (m_strpool.end() <= da && da < m_strpool.finish()) {
        // m_strpool.data() starts after Header, so it can not be mmap.
        TERARK_VERIFY_EQ(m_strpool_mem_type, MemType::User);
        TERARK_VERIFY_LE((byte_t*)(m_da_data + m_da_size), m_strpool.finish());
    } else {
        // m_strpool can be mmap or malloc.
        // in ToplingDB index build, m_strpool_mem_type may be mmap.
        if (da)
            free(da);
    }
}

static const char* GetWalkMethod() {
    const char* walkMethod = "CFS";
    if (auto env = getenv("StrVecSearchCacheDFA_WalkMethod")) {
        if (strcasecmp(env, "BFS") == 0
         || strcasecmp(env, "CFS") == 0
         || strcasecmp(env, "DFS") == 0
        ) {
            walkMethod = env;
        }
        else {
            fprintf(stderr
                , "WARN: env StrVecSearchCacheDFA_WalkMethod=%s is invalid, use default: \"%s\"\n"
                , env, walkMethod);
        }
    }
    return walkMethod;
}

void
DaCacheFixedStrVec::build_cache(size_t stop_range_len) {
    MyAppendOnlyTrie trie;
    profiling pf;
    long long t0 = pf.now();
{
    const byte_t* str = this->m_strpool.data();
    const size_t  fixlen = this->m_fixlen;
    trie.erase_all();
    trie.states.erase_all();
    trie.states.push_back({});
    trie.states[0].m_lo = 0;
    trie.states[0].m_hi = this->m_size;
    AutoFree<CharTarget<size_t> > children(trie.sigma);
    valvec<uint32_t> q1, q2; // queue item is state_id
    q1.push_back(0);
    size_t bfsDepth = 0;
    while (!q1.empty() && bfsDepth < fixlen) {
        for(auto state : q1) {
            size_t lo = trie.states[state].m_lo;
            size_t hi = trie.states[state].m_hi;
            size_t childcnt = 0;
            while (lo < hi) {
                byte_t c = str[fixlen * lo + bfsDepth];
                size_t u = this->upper_bound_at_pos(lo, hi, bfsDepth, c);
                size_t child = trie.new_state();
                if (u - lo > stop_range_len) {
                    q2.push_back(uint32_t(child));
                }
                children[childcnt].ch = c;
                children[childcnt].target = child;
                trie.states[child].m_lo = lo;
                trie.states[child].m_hi = u;
                childcnt++;
                lo = u;
            }
            trie.add_all_move(state, children, childcnt);
        }
        q1.swap(q2);
        q2.erase_all();
        bfsDepth++;
    }
    trie.finish_append();
    trie.shrink_to_fit();
}
    long long t1 = pf.now();
    valvec<uint32_t> t2d, d2t;
    MyDoubleArrayTrie dart;
    dart.build_from(trie, d2t, t2d, GetWalkMethod(), 0);
    for(size_t i = 0; i < d2t.size(); ++i) {
        size_t j = d2t[i];
        if (j < trie.states.size()) {
            dart.states[i].m_lo = trie.states[j].m_lo;
            dart.states[i].m_hi = trie.states[j].m_hi;
        }
    }
    long long t2 = pf.now();
    if (g_showStat) {
        printf("DaCacheFixedStrVec: build dynamic trie time: %7.3f seconds\n", pf.sf(t0,t1));
        printf("DaCacheFixedStrVec: build double array time: %7.3f seconds\n", pf.sf(t1,t2));
        sa_print_stat();
    }
    if (g_useHugePage) {
        dart.useHugePage();
    }
    m_da_data = dart.states.data();
    m_da_size = dart.states.size();
    m_da_free = dart.num_free_states();
    dart.states.risk_release_ownership();
}

static inline
std::pair<size_t, size_t>
s_sa_equal_range(const byte* str, size_t fixlen,
                 size_t lo, size_t hi, size_t depth, byte_t ch) noexcept {
    assert(lo < hi);
    // binary search
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        byte_t hitChr = str[fixlen * mid + depth];
        if (hitChr < ch)
            lo = mid + 1;
        else if (hitChr > ch)
            hi = mid;
        else
            goto Found;
    }
    return std::make_pair(lo, lo);
Found:
    size_t lo1 = lo, hi1 = hi;
    while (lo1 < hi1) {
        size_t mid = (lo1 + hi1) / 2;
        byte_t hitChr = str[fixlen * mid + depth];
        if (hitChr < ch) // lower bound
            lo1 = mid + 1;
        else
            hi1 = mid;
    }
    size_t lo2 = lo + 1, hi2 = hi;
    while (lo2 < hi2) {
        size_t mid = (lo2 + hi2) / 2;
        byte_t hitChr = str[fixlen * mid + depth];
        if (hitChr <= ch) // upper bound
            lo2 = mid + 1;
        else
            hi2 = mid;
    }
    return std::make_pair(lo1, lo2);
}

// many params may get better compiler optimization
static inline
DaCacheFixedStrVec::MatchStatus
s_sa_match(const byte* str, size_t fixlen,
            size_t lo, size_t hi, size_t pos,
            const byte* input, size_t len) noexcept {
    using std::min;
    using std::max;
//	printf("freq =%4zd  pos =%3zd : \33[1;31m%.*s\33[0m%.*s\n", hi-lo, pos
//		, int(pos), input, min(int(len-pos), 40), input + pos);
    assert(hi - lo >= 1); // cache should has a bigger freq
    while (pos < len) {
        auto rng = s_sa_equal_range(str, fixlen, lo, hi, pos, input[pos]);
        if (rng.second != rng.first) {
            lo = rng.first;
            hi = rng.second;
            pos++;
        } else
            break;
    }
//	printf("freq =%4zd  pos =%3zd : \33[1;32m%.*s\33[0m%.*s\n", hi-lo, pos
//		, int(pos), input, min(int(len-pos), 40), input + pos);
    return {lo, hi, pos};
}

terark_flatten
DaCacheFixedStrVec::MatchStatus
DaCacheFixedStrVec::match_max_length(const byte* input, size_t len)
const noexcept {
    assert(m_da_data != NULL);
    minimize(len, this->m_fixlen);
    const byte_t* str = this->m_strpool.data();
    size_t lo = 0, hi = this->m_size, pos = 0, fixlen = this->m_fixlen;
    size_t state = 0;
    const auto lstates = m_da_data;
    while (pos < len) {
        size_t child = lstates[state].m_child0 + input[pos];
        if (terark_likely(lstates[child].m_parent == state)) {
            state = child;
            lo = lstates[child].m_lo;
            hi = lstates[child].m_hi;
            pos++;
        }
        else
            return s_sa_match(str, fixlen, lo, hi, pos, input, len);
    }
    return {lo, hi, pos};
}

terark_flatten
DaCacheFixedStrVec::MatchStatus
DaCacheFixedStrVec::da_match(const byte* input, size_t len)
const noexcept {
    assert(m_da_data != NULL);
    minimize(len, this->m_fixlen);
    size_t lo = 0, hi = this->m_size, pos = 0;
    size_t state = 0;
    const auto lstates = m_da_data;
    while (pos < len) {
        size_t child = lstates[state].m_child0 + input[pos];
        if (terark_likely(lstates[child].m_parent == state)) {
            state = child;
            lo = lstates[child].m_lo;
            hi = lstates[child].m_hi;
            pos++;
        }
        else
            break;
    }
    return {lo, hi, pos};
}

terark_flatten
DaCacheFixedStrVec::MatchStatus
DaCacheFixedStrVec::da_match_with_hole(const byte* input, size_t len,
                                       const uint16_t* holeMeta)
const noexcept {
    assert(m_da_data != NULL);
    size_t lo = 0, hi = this->m_size, pos = 0;
    size_t state = 0;
    const auto lstates = m_da_data;
    while (pos < len) {
        if (holeMeta[pos] < 256) {
            if (byte_t(holeMeta[pos]) == input[pos])
                pos++;
            else
                break;
        }
        else {
            size_t child = lstates[state].m_child0 + input[pos];
            if (terark_likely(lstates[child].m_parent == state)) {
                state = child;
                lo = lstates[child].m_lo;
                hi = lstates[child].m_hi;
                pos++;
            }
            else
                break;
        }
    }
    return {lo, hi, pos};
}

void DaCacheFixedStrVec::sa_print_stat() const {
    printf("DaCacheFixedStrVec: total string length: %10zd\n", this->str_size());
    if (m_da_data) {
        printf("DaCacheFixedStrVec: double array trie states: %10zd\n", m_da_size);
        printf("DaCacheFixedStrVec: double array free states: %10zd\n", m_da_free);
        printf("DaCacheFixedStrVec: double array fill  ratio: %10.8f\n", 1.0*(m_da_size - m_da_free)/m_da_size);
        printf("DaCacheFixedStrVec: double array trie memory: %10zd\n", sizeof(MyDoubleArrayNode) * m_da_size);
    }
}

// echo -n DaCachFx | od -tx8
static constexpr const uint64_t s_magic = 0x7846686361436144; // DaCachFx
struct DaCacheFixedStrVec::MyHeader {
    uint64_t magic = s_magic;
    uint64_t mem_size;
    uint32_t da_size;
    uint32_t da_free;
    uint32_t fixlen;
    uint32_t strnum;
};

size_t DaCacheFixedStrVec::save_mmap(std::function<void(const void*, size_t)> write) const {
    static_assert(sizeof(MyHeader) == 32);
    static_assert(sizeof(MyDoubleArrayNode) == 16);
    size_t da_mem_size = sizeof(MyDoubleArrayNode) * m_da_size;
    MyHeader h;
    h.mem_size = align_up(sizeof(h) + m_strpool.size(), 16) + da_mem_size;
    h.da_size = m_da_size;
    h.da_free = m_da_free;
    h.fixlen = m_fixlen;
    h.strnum = m_size;
    write(&h, sizeof(h));
    write(m_strpool.data(), m_strpool.size());
    static const char zeros[64] = {};
    size_t offset = sizeof(h) + m_strpool.size();
    auto padzeros_align = [&](size_t align) {
        if (offset % align) {
            size_t pad = align - (offset % align);
            write(zeros, pad);
            offset += pad;
        }
    };
    padzeros_align(16);
    write(m_da_data, da_mem_size);
    return h.mem_size;
}

size_t DaCacheFixedStrVec::size_mmap() const {
    size_t da_mem_size = sizeof(MyDoubleArrayNode) * m_da_size;
    return align_up(sizeof(MyHeader) + m_strpool.size(), 16) + da_mem_size;
}

void DaCacheFixedStrVec::load_mmap(const byte_t* mem, size_t len) {
    auto h = (const MyHeader*)(mem);
    TERARK_VERIFY_LE(h->mem_size, len);
    this->clear();
    m_strpool_mem_type = MemType::User;
    m_strpool.risk_set_data((byte_t*)(h + 1));
    m_strpool.risk_set_size(h->fixlen * h->strnum);
    m_strpool.risk_set_capacity(len - sizeof(h));
    m_fixlen = h->fixlen;
    m_size = h->strnum;
    m_da_size = h->da_size;
    m_da_free = h->da_free;

    size_t da_offset = align_up(sizeof(MyHeader) + m_strpool.size(), 16);
    m_da_data = (MyDoubleArrayNode*)(mem + da_offset);

    this->optimize_func();
}

fstring DaCacheFixedStrVec::da_memory() const noexcept {
    return fstring((const char*)m_da_data, sizeof(m_da_data[0]) * m_da_size);
}

} // namespace terark

