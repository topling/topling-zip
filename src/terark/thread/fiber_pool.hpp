//
// Created by leipeng on 2022-08-24 14:24
//

#include "fiber_yield.hpp"
#include <boost/fiber/buffered_channel.hpp>

namespace terark {

class TERARK_DLL_EXPORT FiberPool : public FiberYield {
public:
  static constexpr int MAX_QUEUE_LEN = 256;
  static constexpr int DEFAULT_FIBER_CNT = 16;
  struct task_t {
    void (*func)(void* arg1, size_t arg2, size_t arg3);
    void*  arg1;
    size_t arg2; // theoretically arg1 is enough, we add arg2 & arg3 for common
    size_t arg3; // case optimization to avoid user code alloc memory for args.
                 // also buffered_channel use array to store task_t, with arg2
                 // and arg3, sizeof(task_t) == 32 is power of 2, this helps
                 // compiler optimization
  };
  explicit FiberPool(boost::fibers::context** activepp);
  void update_fiber_count(int count);
  void push(task_t&& task);
  bool try_push(const task_t& task);
  int wait(int timeout_us);
  int wait();
  int fiber_cnt() const { return m_fiber_cnt; }
  int pending_cnt() const { return m_pending_cnt; }
protected:
  void fiber_proc(int fiber_idx);
  int m_fiber_cnt = 0;
  int m_pending_cnt = 0;
  boost::fibers::buffered_channel<task_t> m_channel;
};

} // namespace terark
