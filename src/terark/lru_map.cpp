#include "lru_map.hpp"
#include <terark/util/atomic.hpp>
#include <terark/thread/futex.hpp>

namespace terark {

#if TOPLING_LruMapLock_use_futex

// this futex implementation may dead lock(may missing a FUTEX_WAKE_PRIVATE),
// when dead lock, thread is in FUTEX_WAIT_PRIVATE, but m_lru_futex == 0
void LruMapLock::lock() noexcept {
    //m_mtx.lock();
    // m_lru_futex:
    //  0 : unlocked
    //  1 : locked, has no waiter
    //  2 : locked, has    waiter
    unsigned Old = 0;
    while (!as_atomic(m_lru_futex).compare_exchange_weak(Old, 1, std::memory_order_acq_rel)) {
        if (terark_unlikely(0 == Old)) continue; // false fail
        if (2 == Old ||
            as_atomic(m_lru_futex).compare_exchange_weak(Old, 2, std::memory_order_acq_rel)) {
                                                      // ^^^---- Old must be 1
            if (futex(&m_lru_futex, FUTEX_WAIT_PRIVATE, 2) < 0) {
                int err = errno;
                if (!(EINTR == err || EAGAIN == err))
                    TERARK_DIE("futex(WAIT) = %d: %s", err, strerror(err));
            }
        }
        Old = 0;
    }
}

void LruMapLock::unlock() noexcept {
    //m_mtx.unlock();
    auto Old = as_atomic(m_lru_futex).fetch_sub(1, std::memory_order_acq_rel);
    TERARK_VERIFY_GE(Old, 1);
    if (Old != 1) { // Old == 2
        as_atomic(m_lru_futex).store(0, std::memory_order_release);
        futex(&m_lru_futex, FUTEX_WAKE_PRIVATE, 1); // wake 1 waiter
    }
}

#else

// std::mutex lock/unlock may throw, but we mark this lock/unlock noexcept,
// let c++ rt call std::terminate on lock/unlock really throws
void LruMapLock::lock() noexcept {
    m_mtx.lock();
}
void LruMapLock::unlock() noexcept {
    m_mtx.unlock();
}

#endif

// this function is called in mtx lock
bool LruMapLock::wait_solve(LruValueNodeBase& node) noexcept {
    unsigned state = node.m_lru_next;
    size_t retry = 0;
    while (++retry && inprogress == state) {
        if (futex(&node.m_lru_next, FUTEX_WAIT_PRIVATE, inprogress) < 0) {
            int err = errno;
            if (!(EINTR == err || EAGAIN == err))
                TERARK_DIE("futex(WAIT) = %d: %s", err, strerror(err));
        }
        state = node.m_lru_next;
        if (solve_done == state)
            return true;
        if (solve_fail == state)
            return false;
    }
    TERARK_DIE("state = %d, m_lru_next = %d, retry = %zd", state, node.m_lru_next, retry);
    return false;
}

void LruMapLock::wake_solve(LruValueNodeBase& node, bool ok) noexcept {
    unsigned val = ok ? solve_done : solve_fail;
    as_atomic(node.m_lru_next).store(val, std::memory_order_release);
    if (as_atomic(node.m_lru_refcnt).load(std::memory_order_acquire) >= 2)
        futex(&node.m_lru_next, FUTEX_WAKE_PRIVATE, INT_MAX);
}

} // namespace terark
