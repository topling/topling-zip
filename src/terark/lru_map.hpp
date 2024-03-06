// by leipeng 2021-03-30
#pragma once

#include "hash_strmap.hpp"
#include "gold_hash_map.hpp"
#include <mutex>

namespace terark {

struct TERARK_DLL_EXPORT LruValueNodeBase {
    unsigned m_lru_hitcnt = 1;
    unsigned m_lru_refcnt = 1;
    unsigned m_lru_next = unsigned(-1); // -1 is inprogress
    unsigned m_lru_prev = unsigned(-1);
};

template<class Value>
struct LruValueNode : LruValueNodeBase {
    Value  val;
};

class TERARK_DLL_EXPORT LruMapLock {
protected:
    enum State : unsigned {
        inprogress = unsigned(-1),
        solve_fail = unsigned(-2),
        solve_done = unsigned(-3),
    };
#ifdef TOPLING_LruMapLock_use_futex
    unsigned m_lru_futex = 0;
#else
    std::mutex m_mtx;
#endif
    unsigned m_lru_head = unsigned(-1);
    unsigned m_lru_len = 0; // lru list len
    unsigned m_num_evicting = 0;
    void lock() noexcept;
    void unlock() noexcept;
    bool wait_solve(LruValueNodeBase&) noexcept;
    void wake_solve(LruValueNodeBase&, bool ok) noexcept;
};

template<class Key, class Value, class Map>
class lru_map_impl : private Map, private LruMapLock {
    using Map::lazy_insert_i;
public:
    typedef typename Map::link_t link_t;
    typedef LruValueNode<Value> LruNode;
    lru_map_impl() {
        Map::enable_freelist();
    }
    explicit lru_map_impl(size_t cap) {
        TERARK_VERIFY_GT(cap, 2);
        Map::enable_freelist();
        Map::reserve(cap);
    }
    virtual ~lru_map_impl() = default;
    const Map& risk_get_map() const noexcept { return *this; }
    Map& risk_get_map() noexcept { return *this; }
    void set_lru_cap(size_t cap) noexcept { Map::reserve(cap); }
    size_t get_lru_cap() const noexcept { return Map::capacity(); }
    const Value& lru_val(size_t idx) const noexcept {
        TERARK_ASSERT_LT(idx, Map::end_i());
        assert(Map::is_deleted(idx) == false);
        return Map::val(idx).val;
    }
    Value& lru_val(size_t idx) noexcept {
        TERARK_ASSERT_LT(idx, Map::end_i());
        assert(Map::is_deleted(idx) == false);
        return Map::val(idx).val;
    }
    size_t lru_get(const Key& k) noexcept {
        this->lock();
        size_t idx = Map::find_i(k);
        if (Map::end_i() == idx) {
            this->unlock();
            return size_t(-1);
        }
        auto& node = Map::val(idx);
        if (terark_unlikely(inprogress == node.m_lru_next)) {
            // is in solving, node has at least a ref which is by solver
            TERARK_VERIFY_GE(node.m_lru_refcnt, 1);
            node.m_lru_refcnt++;
            this->unlock(); // do not lock whole lru for long time
            if (wait_solve(node)) {
                return idx;
            }
            else {
                TERARK_VERIFY_EQ(solve_fail, node.m_lru_next);
                this->lock();
                if (0 == --node.m_lru_refcnt) // am I the last ref ?
                    Map::erase_i(idx);
                this->unlock();
                return size_t(-1); // fail
            }
        }
        else if (terark_unlikely(solve_fail == node.m_lru_next)) {
            TERARK_VERIFY_GE(node.m_lru_refcnt, 1);
            this->unlock();
            return size_t(-1);
        }
        else {
            if (0 == node.m_lru_refcnt) {
                remove_node(idx, node);
            }
            TERARK_VERIFY_EQ(node.m_lru_next, solve_done); // must not in lru
            node.m_lru_refcnt++;
            node.m_lru_hitcnt++;
            this->unlock();
            return idx;
        }
    }
    template<class SolveMiss>
    std::pair<size_t, bool> lru_add(const Key& k, SolveMiss solve) noexcept {
        this->lock();
        auto  ib = lazy_insert_i(k, &default_cons<LruNode>, evict_on_full);
        auto& node = Map::val(ib.first);
        if (ib.second) {
            TERARK_VERIFY_EQ(node.m_lru_refcnt, 1);
            this->unlock(); // do not lock whole lru for long time
            // I have refcnt, 'node' will not be changed by other threads
            // until I release this 'node'
            bool ok = solve(k, &node.val); // may take long time
            wake_solve(node, ok); // lock is not hold on calling
            if (!ok)
                goto SolveFailed;
        }
        else if (terark_unlikely(inprogress == node.m_lru_next)) {
            // is in solving, node has at least ref which is by solver
            TERARK_VERIFY_GE(node.m_lru_refcnt, 1);
            node.m_lru_refcnt++;
            this->unlock(); // do not lock whole lru for long time
            if (!wait_solve(node))
                goto SolveFailed;
        }
        else if (terark_unlikely(solve_fail == node.m_lru_next)) {
            TERARK_VERIFY_GE(node.m_lru_refcnt, 1);
            this->unlock();
            ib.first = size_t(-1);
        }
        else {
            if (0 == node.m_lru_refcnt) {
                remove_node(ib.first, node);
            }
            TERARK_VERIFY_EQ(node.m_lru_next, solve_done); // must not in lru
            node.m_lru_refcnt++;
            node.m_lru_hitcnt++;
            this->unlock();
        }
        if (false) { SolveFailed:
            auto& node = Map::val(ib.first);
            TERARK_VERIFY_EQ(solve_fail, node.m_lru_next);
            this->lock();
            if (0 == --node.m_lru_refcnt) // am I the last ref ?
                Map::erase_i(ib.first);
            this->unlock();
            ib.first = size_t(-1); // fail
        }
        return ib;
    }
    void lru_release(size_t slot) noexcept {
        this->lock();
        TERARK_ASSERT_LT(slot, Map::end_i());
        assert(Map::is_deleted(slot) == false);
        auto& node = Map::val(slot);
        auto  old_refcnt = node.m_lru_refcnt--;
        TERARK_ASSERT_GT(old_refcnt, 0);
        if (1 == old_refcnt) {
            insert_as_new_head(slot, node);
        }
        this->unlock();
    }
protected:
    void insert_as_new_head(link_t slot) noexcept {
        insert_as_new_head(slot, Map::val(slot));
    }
    void insert_as_new_head(link_t slot, LruNode& node) noexcept {
        TERARK_ASSERT_EZ(node.m_lru_refcnt);
        if (link_t(-1) == m_lru_head) {
            TERARK_ASSERT_EZ(m_lru_len);
            node.m_lru_next = slot;
            node.m_lru_prev = slot;
        }
        else {
            TERARK_ASSERT_GT(m_lru_len, 0);
            auto head = m_lru_head;
            auto tail = Map::val(head).m_lru_prev;
            node.m_lru_next = head;
            node.m_lru_prev = tail;
            Map::val(tail).m_lru_next = slot;
            Map::val(head).m_lru_prev = slot;
        }
        m_lru_head = slot;
        m_lru_len++;
    }
    void remove_node(size_t slot) noexcept {
        remove_node(slot, Map::val(slot));
    }
    void remove_node(size_t slot, LruNode& node) noexcept {
        TERARK_ASSERT_EZ(node.m_lru_refcnt);
        auto prev = node.m_lru_prev;
        auto next = node.m_lru_next;
        Map::val(prev).m_lru_next = next; // remove node
        Map::val(next).m_lru_prev = prev;
        if (slot == m_lru_head) {
            if (1 == m_lru_len)
                m_lru_head = link_t(-1);
            else
                m_lru_head = next;
        }
        node.m_lru_prev = solve_done;
        node.m_lru_next = solve_done;
        m_lru_len--;
    }
    static bool evict_on_full(Map* map) {
        auto self = static_cast<lru_map_impl*>(map);
        if (self->size() >= self->capacity())
            self->evict_least_recent();
        return true;
    };
    terark_no_inline void evict_least_recent() {
        // evict least recently used
        // this function is in mutex lock
        TERARK_VERIFY_EQ(Map::size(), Map::capacity());
        if (link_t(-1) != m_lru_head) {
            assert(m_lru_len > 0);
            link_t victim = Map::val(m_lru_head).m_lru_prev;
            LruNode& node = Map::val(victim);
            TERARK_ASSERT_EZ(node.m_lru_refcnt);
            remove_node(victim, node);
            node.m_lru_prev = inprogress; // for iterate all elem
            this->risk_unlink(victim); // node.val is still valid, and -
            this->m_num_evicting++; // victim is leak until risk_slot_free
            this->unlock(); // this is safe
            // 1. now victim is not visible through find/insert, so there is no
            //    any other threads can access this victim.
            // 2. if we iterate all elem throw Map, we must in lock, and using
            //    node.m_lru_prev == inprogress to check victim.
            lru_evict(Map::key(victim), &node.val); // may take long time
            this->lock(); // re-lock
            this->risk_slot_free(victim);
            this->m_num_evicting--;
        }
        else { // can not evict
            TERARK_VERIFY_EZ(m_lru_len);
            TERARK_DIE("LruList is empty but size == cap : %zd, "
                       "may be some leak", Map::capacity());
        }
    }
    virtual void lru_evict(const Key&, Value*) = 0;
};

template< class Value
		, class HashFunc = fstring_func::IF_SP_ALIGN(hash_align, hash)
		, class KeyEqual = fstring_func::IF_SP_ALIGN(equal_align, equal)
		, class ValuePlace = std::conditional_t
                < sizeof(Value) % sizeof(intptr_t) == 0 && sizeof(Value) <= 48
				, ValueInline
				, ValueOut // If Value is empty, ValueOut will not use memory
			    >
		, class CopyStrategy = FastCopy
		, class LinkTp = unsigned int // could be unsigned short for small map
		, class HashTp = HSM_HashTp
        >
using lru_hash_strmap = lru_map_impl<fstring, Value,
          hash_strmap<LruValueNode<Value>, HashFunc, KeyEqual, ValuePlace,
                      CopyStrategy, LinkTp, HashTp, true/*WithFreeList*/> >;

template<class Key, class Value, class... OtherArgs>
using lru_gold_hash_map = lru_map_impl<Key, Value,
          gold_hash_map<Key, LruValueNode<Value>, OtherArgs...> >;

} // namespace terark
