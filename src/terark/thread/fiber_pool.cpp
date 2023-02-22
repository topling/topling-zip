//
// Created by leipeng on 2022-08-24 14:24
//

#include "fiber_pool.hpp"
#include <boost/fiber/fiber.hpp>
#include <boost/fiber/protected_fixedsize_stack.hpp>
#include <terark/fstring.hpp>

namespace terark {

FiberPool::FiberPool(boost::fibers::context** activepp)
    : FiberYield(activepp), m_channel(MAX_QUEUE_LEN) {
  update_fiber_count(DEFAULT_FIBER_CNT);
}

void FiberPool::fiber_proc(int fiber_idx) {
  using boost::fibers::channel_op_status;
  task_t task;
  while (fiber_idx < m_fiber_cnt &&
         m_channel.pop(task) == channel_op_status::success) {
    task.func(task.arg1, task.arg2, task.arg3);
    m_pending_cnt--;
  }
}

void FiberPool::update_fiber_count(int count) {
  if (count <= 0) {
    return;
  }
  static const long stack_size = ParseSizeXiB(getenv("TOPLING_FIBER_STACK_SIZE"), 128*1024);
  count = std::min<int>(count, +MAX_QUEUE_LEN);
  for (int i = m_fiber_cnt; i < count; ++i) {
    using namespace boost::fibers;
  //using stack_t = default_stack;
    using stack_t = protected_fixedsize_stack;
    fiber(std::allocator_arg, stack_t(stack_size), &FiberPool::fiber_proc, this, i).detach();
  }
  m_fiber_cnt = count;
}

void FiberPool::push(task_t&& task) {
  m_channel.push(std::move(task));
  m_pending_cnt++;
}

bool FiberPool::try_push(const task_t& task) {
  using boost::fibers::channel_op_status;
  if (m_channel.try_push(task) == channel_op_status::success) {
    m_pending_cnt++;
    return true;
  }
  return false;
}

int FiberPool::wait(int timeout_us) {
  int old_pending_count = m_pending_cnt;
  if (old_pending_count == 0) {
    return 0;
  }

  using namespace std::chrono;

  // do not use sleep_for, because we want to return as soon as possible
  // boost::this_fiber::sleep_for(microseconds(timeout_us));
  // return tls->pending_count - old_pending_count;

  auto start = std::chrono::system_clock::now();
  while (true) {
    // boost::this_fiber::yield(); // wait once
    this->unchecked_yield();
    if (m_pending_cnt > 0) {
      auto now = system_clock::now();
      auto dur = duration_cast<microseconds>(now - start).count();
      if (dur >= timeout_us) {
        return int(m_pending_cnt - old_pending_count - 1);  // negtive
      }
    } else {
      break;
    }
  }
  return int(old_pending_count);
}

int FiberPool::wait() {
  int cnt = m_pending_cnt;
  while (m_pending_cnt > 0) {
    // boost::this_fiber::yield(); // wait once
    this->unchecked_yield();
  }
  return int(cnt);
}

} // namespace terark
