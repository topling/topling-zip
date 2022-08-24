//
// Created by leipeng on 2019-08-22.
//

#include "fiber_aio.hpp"
#include <boost/predef.h>

#if BOOST_OS_LINUX
  #include <libaio.h> // linux native aio
  #include <liburing.h>
#endif

#if BOOST_OS_WINDOWS
	#define NOMINMAX
	#define WIN32_LEAN_AND_MEAN
	#include <io.h>
	#include <Windows.h>
#else
  #include <aio.h> // posix aio
  #include <sys/types.h>
  #include <sys/mman.h>
#endif

#include "fiber_yield.hpp"
#include <terark/stdtypes.hpp>
#include <terark/fstring.hpp>
#include <terark/util/atomic.hpp>
#include <terark/util/enum.hpp>
#include <terark/util/vm_util.hpp>
#include <boost/fiber/all.hpp>
#include <boost/lockfree/queue.hpp>

#if defined(__linux__)
  #include <linux/version.h>
#endif

namespace terark {

#if defined(__GNUC__)
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

TERARK_ENUM_CLASS(IoProvider, int,
  //sync,
  posix,
  aio,
  uring
);

#if BOOST_OS_LINUX

static IoProvider g_io_provider = []{
  const char* env = getenv("TOPLING_IO_PROVIDER");
  return enum_value(env ? env : "uring", IoProvider::uring);
}();

static std::atomic<size_t> g_ft_num;

#define aio_debug(...)
//#define aio_debug(fmt, ...) fprintf(stderr, "DEBUG: %s:%d:%s: " fmt "\n", __FILE__, __LINE__, BOOST_CURRENT_FUNCTION, ##__VA_ARGS__)

#define FIBER_AIO_VERIFY(expr) \
  do { \
    int ret = expr; \
    if (ret) TERARK_DIE("%s = %s", #expr, strerror(-ret)); \
  } while (0)

struct io_return {
  boost::fibers::context* fctx;
  intptr_t len;
  int err;
  bool done;
};

typedef boost::lockfree::queue<struct iocb*, boost::lockfree::fixed_sized<true>>
        io_queue_t;
io_queue_t* dt_io_queue();

class io_fiber_base {
protected:
  static const int reap_batch = 32;
  enum class state {
    ready,
    running,
    stopping,
    stopped,
  };
  FiberYield           m_fy;
  volatile state       m_state;
  size_t               ft_num;
  unsigned long long   counter;
  boost::fibers::fiber io_fiber;
  volatile size_t      io_reqnum = 0;

  void fiber_proc() {
    m_state = state::running;
    while (state::running == m_state) {
      io_reap();
      yield();
      counter++;
    }
    TERARK_VERIFY_EQ(state::stopping, m_state);
    m_state = state::stopped;
  }

  virtual void io_reap() = 0;

  io_fiber_base(boost::fibers::context** pp)
    : m_fy(pp)
    , io_fiber([this]() { this->fiber_proc(); })
  {
    m_state = state::ready;
    counter = 0;
    ft_num = g_ft_num++;
    aio_debug("ft_num = %zd", ft_num);
  }
  void wait_for_finish() {
    aio_debug("ft_num = %zd, counter = %llu ...", ft_num, counter);
    m_state = state::stopping;
    while (state::stopping == m_state) {
      aio_debug("ft_num = %zd, counter = %llu, yield ...", ft_num, counter);
      yield();
    }
    TERARK_VERIFY(state::stopped == m_state);
    io_fiber.join();
    aio_debug("ft_num = %zd, counter = %llu, done", ft_num, counter);
    TERARK_VERIFY(0 == io_reqnum);
  }
public:
  inline void yield() { m_fy.unchecked_yield(); }
};

class io_fiber_aio : public io_fiber_base {
  io_context_t         io_ctx = 0;
  struct io_event      io_events[reap_batch];

  #define AIO_CMD_ENUM(cmd) AIO_##cmd = IO_CMD_##cmd
  TERARK_ENUM_PLAIN_INCLASS(aio_cmd, int,
    AIO_CMD_ENUM(PREAD),
    AIO_CMD_ENUM(PWRITE),
    AIO_CMD_ENUM(FSYNC),
    AIO_CMD_ENUM(FDSYNC),
    AIO_CMD_ENUM(POLL),
    AIO_CMD_ENUM(NOOP),
    AIO_CMD_ENUM(PREADV)
  );

  void io_reap() override {
    for (;;) {
      int ret = io_getevents(io_ctx, 0, reap_batch, io_events, NULL);
      if (ret < 0) {
        int err = -ret;
        if (EAGAIN == err)
          yield();
        else
          fprintf(stderr, "ERROR: ft_num = %zd, io_getevents(nr=%d) = %s\n", ft_num, reap_batch, strerror(err));
      }
      else {
        for (int i = 0; i < ret; i++) {
          io_return* ior = (io_return*)(io_events[i].data);
          ior->len = io_events[i].res;
          ior->err = io_events[i].res2;
          ior->done = true;
          m_fy.unchecked_notify(&ior->fctx);
        }
        io_reqnum -= ret;
        if (ret < reap_batch)
          break;
      }
    }
  }

public:
  intptr_t exec_io(int fd, void* buf, size_t len, off_t offset, int cmd) {
    io_return io_ret = {nullptr, 0, -1, false};
    struct iocb io = {0};
    io.data = &io_ret;
    io.aio_lio_opcode = cmd;
    io.aio_fildes = fd;
    io.u.c.buf = buf;
    io.u.c.nbytes = len;
    io.u.c.offset = offset;
    struct iocb* iop = &io;
    while (true) {
      int ret = io_submit(io_ctx, 1, &iop);
      if (ret < 0) {
        int err = -ret;
        if (EAGAIN == err) {
          yield();
          continue;
        }
        fprintf(stderr, "ERROR: ft_num = %zd, len = %zd, offset = %lld, cmd = %s, io_submit(nr=1) = %s\n",
                ft_num, len, (long long)offset, enum_stdstr(aio_cmd(cmd)).c_str(), strerror(err));
        errno = err;
        return -1;
      }
      break;
    }
    io_reqnum++;
    m_fy.unchecked_wait(&io_ret.fctx);
    assert(io_ret.done);
    if (io_ret.err) {
      errno = io_ret.err;
    }
    return io_ret.len;
  }

  intptr_t dt_exec_io(int fd, void* buf, size_t len, off_t offset, int cmd) {
    io_return io_ret = {nullptr, 0, -1, false};
    struct iocb io = {0};
    io.data = &io_ret;
    io.aio_lio_opcode = cmd;
    io.aio_fildes = fd;
    io.u.c.buf = buf;
    io.u.c.nbytes = len;
    io.u.c.offset = offset;
    auto queue = dt_io_queue();
    while (!queue->bounded_push(&io)) yield();
    size_t loop = 0;
    do {
      // io is performed in another thread, we don't know when it's finished,
      // so we poll the flag by yield fiber or yield thread
      if (++loop % 256)
        yield();
      else
        std::this_thread::yield();
    } while (!as_atomic(io_ret.done).load(std::memory_order_acquire));
    if (io_ret.err) {
      errno = io_ret.err;
    }
    return io_ret.len;
  }

  io_fiber_aio(boost::fibers::context** pp) : io_fiber_base(pp) {
    int maxevents = reap_batch*4 - 1;
    FIBER_AIO_VERIFY(io_setup(maxevents, &io_ctx));
  }

  ~io_fiber_aio() {
    wait_for_finish();
    FIBER_AIO_VERIFY(io_destroy(io_ctx));
  }
};

class io_fiber_uring : public io_fiber_base {
  io_uring ring;
  int tobe_submit = 0;

  void io_reap() override {
    if (tobe_submit > 0) {
      int ret = io_uring_submit_and_wait(&ring, io_reqnum ? 0 : 1);
      if (ret < 0) {
        if (-EAGAIN != ret) {
          fprintf(stderr, "ERROR: ft_num = %zd, io_uring_submit, tobe_submit = %d, %s\n",
                  ft_num, tobe_submit, strerror(-ret));
        }
      }
      else {
        tobe_submit -= ret;
        io_reqnum += ret;
      }
    }
    while (io_reqnum) {
      io_uring_cqe* cqe = nullptr;
      int ret = io_uring_wait_cqe(&ring, &cqe);
      if (ret < 0) {
        int err = -ret;
        if (EAGAIN == err)
          yield();
        else
          fprintf(stderr, "ERROR: ft_num = %zd, io_uring_wait_cqe() = %s\n",
                  ft_num, strerror(err));
      }
      else {
        io_return* io_ret = (io_return*)io_uring_cqe_get_data(cqe);
        io_ret->done = true;
        if (terark_likely(cqe->res >= 0)) { // success, normal case
          io_ret->len = cqe->res;
        }
        else { // error
          io_ret->err = -cqe->res;
          if (cqe->res != -EAGAIN)
            TERARK_DIE("cqe failed: %s\n", strerror(-cqe->res));
        }
        io_uring_cqe_seen(&ring, cqe);
        io_reqnum--;
        m_fy.unchecked_notify(&io_ret->fctx);
      }
    }
  }

public:
  intptr_t
  exec_io(int fd, void* buf, size_t len, off_t offset, int cmd) {
    io_return io_ret = {nullptr, 0, 0, false};
    io_uring_sqe* sqe;
    while (terark_unlikely((sqe = io_uring_get_sqe(&ring)) == nullptr)) {
      io_reap();
    }
    io_uring_prep_rw(cmd, sqe, fd, buf, len, offset);
    io_uring_sqe_set_data(sqe, &io_ret);
    tobe_submit++;
    m_fy.unchecked_wait(&io_ret.fctx);
    assert(io_ret.done);
    if (io_ret.err) {
      errno = io_ret.err;
    }
    return io_ret.len;
  }

  io_fiber_uring(boost::fibers::context** pp) : io_fiber_base(pp) {
    int queue_depth = reap_batch*4;
    FIBER_AIO_VERIFY(io_uring_queue_init(queue_depth, &ring, 0));
  }

  ~io_fiber_uring() {
    wait_for_finish();
    io_uring_queue_exit(&ring);
  }
};

static io_fiber_aio& tls_io_fiber_aio() {
  using boost::fibers::context;
  static thread_local io_fiber_aio io_fiber(context::active_pp());
  return io_fiber;
}

static io_fiber_uring& tls_io_fiber_uring() {
  using boost::fibers::context;
  static thread_local io_fiber_uring io_fiber(context::active_pp());
  return io_fiber;
}

// dt_ means 'dedicated thread'
struct DT_ResetOnExitPtr {
  std::atomic<io_queue_t*> ptr;
  DT_ResetOnExitPtr();
  ~DT_ResetOnExitPtr() { ptr = nullptr; }
};
static void dt_func(DT_ResetOnExitPtr* p_tls) {
  io_queue_t queue(1023);
  p_tls->ptr = &queue;
  io_context_t io_ctx = 0;
  constexpr int batch = 64;
  FIBER_AIO_VERIFY(io_setup(batch*4 - 1, &io_ctx));
  struct iocb*    io_batch[batch];
  struct io_event io_events[batch];
  intptr_t req = 0, submits = 0, reaps = 0;
  while (p_tls->ptr.load(std::memory_order_relaxed)) {
    while (req < batch && queue.pop(io_batch[req])) req++;
    int works = 0;
    if (req) {
      int ret = io_submit(io_ctx, req, io_batch);
      if (ret < 0) {
        int err = -ret;
        if (EAGAIN != err)
          TERARK_DIE("io_submit(nr=%zd) = %s\n", req, strerror(err));
      }
      else if (ret > 0) {
        submits += ret;
        works += ret;
        req -= ret;
        if (req)
          std::copy_n(io_batch + ret, req, io_batch);
      }
    }
    if (reaps < submits) {
      int ret = io_getevents(io_ctx, 1, batch, io_events, NULL);
      if (ret < 0) {
        int err = -ret;
        if (EAGAIN != err)
          fprintf(stderr, "ERROR: %s:%d: io_getevents(nr=%d) = %s\n", __FILE__, __LINE__, batch, strerror(err));
      }
      else {
        for (int i = 0; i < ret; i++) {
          io_return* ior = (io_return*)(io_events[i].data);
          ior->len = io_events[i].res;
          ior->err = io_events[i].res2;
          as_atomic(ior->done).store(true, std::memory_order_release);
        }
        reaps += ret;
        works += ret;
      }
    }
    if (0 == works)
      std::this_thread::yield();
  }
  FIBER_AIO_VERIFY(io_destroy(io_ctx));
}
DT_ResetOnExitPtr::DT_ResetOnExitPtr() {
  ptr = nullptr;
  std::thread(std::bind(&dt_func, this)).detach();
}
io_queue_t* dt_io_queue() {
  static DT_ResetOnExitPtr p_tls;
  io_queue_t* q;
  while (nullptr == (q = p_tls.ptr.load())) {
    std::this_thread::yield();
  }
  return q;
}

#endif

TERARK_DLL_EXPORT
intptr_t fiber_aio_read(int fd, void* buf, size_t len, off_t offset) {
  switch (g_io_provider) {
  default:
    TERARK_DIE("Not Supported aio_method = %s", enum_cstr(g_io_provider));
#if BOOST_OS_LINUX
  case IoProvider::aio:
    return tls_io_fiber_aio().exec_io(fd, buf, len, offset, IO_CMD_PREAD);
  case IoProvider::uring:
  if (g_linux_kernel_version >= KERNEL_VERSION(5,6,0)) {
    return tls_io_fiber_uring().exec_io(fd, buf, len, offset, IORING_OP_READ);
  } else {
    struct iovec iov{buf, len};
    return tls_io_fiber_uring().exec_io(fd, &iov, 1, offset, IORING_OP_READV);
  }
#endif
  case IoProvider::posix:
#if BOOST_OS_WINDOWS
  TERARK_DIE("Not Supported for Windows");
#else
  struct aiocb acb = {0};
  acb.aio_fildes = fd;
  acb.aio_offset = offset;
  acb.aio_buf = buf;
  acb.aio_nbytes = len;
  int err = aio_read(&acb);
  if (err) {
    return -1;
  }
  do {
    boost::this_fiber::yield();
    err = aio_error(&acb);
  } while (EINPROGRESS == err);

  if (err) {
    return -1;
  }
  return aio_return(&acb);
#endif
  } // switch
  TERARK_DIE("Should not goes here");
}

static const size_t MY_AIO_PAGE_SIZE = 4096;

TERARK_DLL_EXPORT
void fiber_aio_need(const void* buf, size_t len) {
#if BOOST_OS_WINDOWS
		WIN32_MEMORY_RANGE_ENTRY vm;
		vm.VirtualAddress = (void*)buf;
		vm.NumberOfBytes  = len;
		PrefetchVirtualMemory(GetCurrentProcess(), 1, &vm, 0);
#elif !defined(__CYGWIN__)
    len += size_t(buf) & (MY_AIO_PAGE_SIZE-1);
    buf  = (const void*)(size_t(buf) & ~(MY_AIO_PAGE_SIZE-1));
    size_t len2 = std::min<size_t>(len, 8*MY_AIO_PAGE_SIZE);
    union {
        uint64_t val;
    #if BOOST_OS_LINUX
        unsigned char  vec[8];
    #else
        char  vec[8];
    #endif
    } uv;  uv.val = 0x0101010101010101ULL;
    int err = mincore((void*)buf, len2, uv.vec);
    if (0 == err) {
        if (0x0101010101010101ULL != uv.val) {
            posix_madvise((void*)buf, len, POSIX_MADV_WILLNEED);
        }
        if (0 == uv.vec[0]) {
            boost::this_fiber::yield(); // just yield once
        }
    }
#endif
}

TERARK_DLL_EXPORT
intptr_t fiber_aio_write(int fd, const void* buf, size_t len, off_t offset) {
  switch (g_io_provider) {
  default:
    TERARK_DIE("Not Supported aio_method = %s", enum_cstr(g_io_provider));
#if BOOST_OS_LINUX
  case IoProvider::aio:
    return tls_io_fiber_aio().exec_io(fd, (void*)buf, len, offset, IO_CMD_PWRITE);
  case IoProvider::uring:
  if (g_linux_kernel_version >= KERNEL_VERSION(5,6,0)) {
    return tls_io_fiber_uring().exec_io(fd, (void*)buf, len, offset, IORING_OP_WRITE);
  } else {
    struct iovec iov{(void*)buf, len};
    return tls_io_fiber_uring().exec_io(fd, &iov, 1, offset, IORING_OP_WRITEV);
  }
#endif
  case IoProvider::posix:
#if BOOST_OS_WINDOWS
  TERARK_DIE("Not Supported for Windows");
#else
  struct aiocb acb = {0};
  acb.aio_fildes = fd;
  acb.aio_offset = offset;
  acb.aio_buf = (void*)buf;
  acb.aio_nbytes = len;
  int err = aio_write(&acb);
  if (err) {
    return -1;
  }
  do {
    boost::this_fiber::yield();
    err = aio_error(&acb);
  } while (EINPROGRESS == err);

  if (err) {
    return -1;
  }
  return aio_return(&acb);
#endif
  } // switch
  TERARK_DIE("Should not goes here");
}

TERARK_DLL_EXPORT
intptr_t fiber_put_write(int fd, const void* buf, size_t len, off_t offset) {
  switch (g_io_provider) {
  default:
    TERARK_DIE("Not Supported aio_method = %s", enum_cstr(g_io_provider));
#if BOOST_OS_LINUX
  case IoProvider::aio:
    return tls_io_fiber_aio().dt_exec_io(fd, (void*)buf, len, offset, IO_CMD_PWRITE);
#else
#endif
  } // switch
  TERARK_DIE("Not Supported platform");
}

} // namespace terark
