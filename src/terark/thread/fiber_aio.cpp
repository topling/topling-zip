//
// Created by leipeng on 2019-08-22.
//

#include "fiber_aio.hpp"
#include <boost/predef.h>

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
#include <terark/valvec.hpp>
#include <boost/fiber/all.hpp>
#include <boost/lockfree/queue.hpp>

#if defined(__linux__)
  #include <linux/version.h>
  #include <libaio.h> // linux native aio
  #if defined(TOPLING_IO_WITH_URING)
    #if TOPLING_IO_WITH_URING // mandatory io uring
      #include <liburing.h>
      #define TOPLING_IO_HAS_URING
    #elif LINUX_VERSION_CODE >= KERNEL_VERSION(5,1,0)
      #pragma message "the kernel support io uring but it is mandatory disabled by -D TOPLING_IO_WITH_URING=0"
    #endif
  #else
    #if LINUX_VERSION_CODE >= KERNEL_VERSION(5,1,0)
      #include <liburing.h>
      #define TOPLING_IO_HAS_URING
    #else
      #pragma message "the kernel does not support io uring, to mandatory compile with io uring, add compile flag: -D TOPLING_IO_WITH_URING=1"
    #endif
  #endif
#endif

namespace terark {

#if defined(__GNUC__)
    #pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

TERARK_ENUM_CLASS(IoProvider, int,
  sync,
  posix,
  aio,
  uring
);

#if BOOST_OS_LINUX

TERARK_DLL_EXPORT int get_linux_kernel_version(); // defined in vm_util.cpp
const static IoProvider g_io_provider = []{
  const char* env = getenv("TOPLING_IO_PROVIDER");
  IoProvider prov = enum_value(env ? env : "uring", IoProvider::uring);
#if defined(TOPLING_IO_HAS_URING)
  if (nullptr == env && get_linux_kernel_version() < KERNEL_VERSION(5,1,0)) {
    fprintf(stderr,
R"(WARN: env TOPLING_IO_PROVIDER is not defined, and linux kernel is too old,
      will fallback to posix aio. If you want io uring in case of your old
      kernel has back-ported io uring, please explicitly define
      env TOPLING_IO_PROVIDER=uring !
)");
    prov = IoProvider::posix;
  }
#else
  if (IoProvider::uring == prov) {
    fprintf(stderr, "WARN: Program is compiled without io uring, fallback to posix aio\n");
    prov = IoProvider::posix;
  }
#endif
  return prov;
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
    , io_fiber(&io_fiber_base::fiber_proc, this)
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

#if defined(TOPLING_IO_HAS_URING)
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

  intptr_t madv(void* addr, size_t len, int advice) {
    io_return io_ret = {nullptr, 0, 0, false};
    io_uring_sqe* sqe;
    while (terark_unlikely((sqe = io_uring_get_sqe(&ring)) == nullptr)) {
      io_reap();
    }
    // io_uring_prep_rw(cmd, sqe, fd, buf, len, offset);
    // just this line diff to exec_io
    io_uring_prep_madvise(sqe, addr, len, advice);
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
    int queue_depth = (int)getEnvLong("TOPLING_IO_URING_QUEUE_DEPTH", 32);
    maximize(queue_depth, 1);
    minimize(queue_depth, 1024);
    FIBER_AIO_VERIFY(io_uring_queue_init(queue_depth, &ring, 0));
  }

  ~io_fiber_uring() {
    wait_for_finish();
    io_uring_queue_exit(&ring);
  }
};
#endif // TOPLING_IO_HAS_URING

static io_fiber_aio& tls_io_fiber_aio() {
  using boost::fibers::context;
  static thread_local io_fiber_aio io_fiber(context::active_pp());
  return io_fiber;
}

#if defined(TOPLING_IO_HAS_URING)
static io_fiber_uring& tls_io_fiber_uring() {
  using boost::fibers::context;
  static thread_local io_fiber_uring io_fiber(context::active_pp());
  return io_fiber;
}
#endif

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

class io_fiber_posix : public io_fiber_base {
  struct fiber_aiocb : public aiocb {
    boost::fibers::context* fctx;
    int err;
  };
  valvec<fiber_aiocb*> m_req;

  void io_reap() override {
    if (m_req.empty())
      return;
    if (aio_suspend((aiocb**)m_req.data(), (int)m_req.size(), NULL) < 0) {
      int err = errno;
      if (EAGAIN == err || EINTR == err)
        return;
      else
        TERARK_DIE("aio_suspend(num=%zd) = %s", m_req.size(), strerror(err));
    }
    size_t h = 0;
    for (size_t i = 0, n = m_req.size(); i < n; i++) {
      auto acb = m_req[i];
      int err = aio_error(acb);
      if (EINPROGRESS == err) {
        m_req[h++] = acb;
      } else {
        acb->err = err;
        m_fy.unchecked_notify(&acb->fctx);
      }
    }
    m_req.risk_set_size(h);
  }

public:
  intptr_t exec_io(int fd, void* buf, size_t len, off_t offset,
                   int(*aio_func)(aiocb*)) {
    fiber_aiocb acb; memset(&acb, 0, sizeof(acb));
    acb.aio_fildes = fd;
    acb.aio_offset = offset;
    acb.aio_buf = buf;
    acb.aio_nbytes = len;
    int err = (*aio_func)(&acb);
    if (err) {
      return -1;
    }
    m_req.push_back(&acb);
    m_fy.unchecked_wait(&acb.fctx);
    errno = acb.err;
    return aio_return(&acb);
  }

  io_fiber_posix(boost::fibers::context** pp) : io_fiber_base(pp) {
    int threads = (int)getEnvLong("TOPLING_IO_POSIX_THREADS", 0);
    if (threads > 0) {
      struct aioinit init = {};
      init.aio_threads = threads;
      init.aio_num = threads * 4;
      aio_init(&init); // return is void
    } else {
      // do not call aio_init, use posix aio default conf
      threads = 20; // glib aio default
    }
    m_req.reserve(threads * 4);
  }

  ~io_fiber_posix() {
    wait_for_finish();
  }
};
static io_fiber_posix& tls_io_fiber_posix() {
  using boost::fibers::context;
  static thread_local io_fiber_posix io_fiber(context::active_pp());
  return io_fiber;
}

TERARK_DLL_EXPORT
intptr_t fiber_aio_read(int fd, void* buf, size_t len, off_t offset) {
  switch (g_io_provider) {
  default:
    TERARK_DIE("Not Supported aio_method = %s", enum_cstr(g_io_provider));
  case IoProvider::sync:
    return pread(fd, buf, len, offset);
#if BOOST_OS_LINUX
  case IoProvider::aio:
    return tls_io_fiber_aio().exec_io(fd, buf, len, offset, IO_CMD_PREAD);
#endif
#if defined(TOPLING_IO_HAS_URING)
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
  return tls_io_fiber_posix().exec_io(fd, buf, len, offset, &aio_read);
#endif
  } // switch
  TERARK_DIE("Should not goes here");
}

static const size_t MY_AIO_PAGE_SIZE = 4096;

TERARK_DLL_EXPORT
void fiber_aio_vm_prefetch(const void* buf, size_t len) {
#if defined(TOPLING_IO_HAS_URING)
  size_t lower = size_t(buf) & ~(MY_AIO_PAGE_SIZE-1);
  size_t upper = size_t(buf) + len;
  len = upper - lower;
  int populate_read = 22; // MADV_POPULATE_READ is just in kernel 5.14+
  switch (g_io_provider) {
  default: break; // do nothing
  case IoProvider::sync: // ignore errno
    madvise((void*)lower, len, populate_read);
    break;
  case IoProvider::uring: // ignore errno
    tls_io_fiber_uring().madv((void*)lower, len, populate_read);
    break;
  }
#endif
}

TERARK_DLL_EXPORT
intptr_t fiber_aio_write(int fd, const void* buf, size_t len, off_t offset) {
  switch (g_io_provider) {
  default:
    TERARK_DIE("Not Supported aio_method = %s", enum_cstr(g_io_provider));
  case IoProvider::sync:
    return pwrite(fd, buf, len, offset);
#if BOOST_OS_LINUX
  case IoProvider::aio:
    return tls_io_fiber_aio().exec_io(fd, (void*)buf, len, offset, IO_CMD_PWRITE);
#endif
#if defined(TOPLING_IO_HAS_URING)
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
  return tls_io_fiber_posix().exec_io(fd, (void*)buf, len, offset, &aio_write);
#endif
  } // switch
  TERARK_DIE("Should not goes here");
}

TERARK_DLL_EXPORT
intptr_t fiber_put_write(int fd, const void* buf, size_t len, off_t offset) {
  switch (g_io_provider) {
  default:
    TERARK_DIE("Not Supported aio_method = %s", enum_cstr(g_io_provider));
  case IoProvider::sync:
    return pwrite(fd, buf, len, offset);
#if BOOST_OS_LINUX
  case IoProvider::aio:
    return tls_io_fiber_aio().dt_exec_io(fd, (void*)buf, len, offset, IO_CMD_PWRITE);
#else
#endif
  } // switch
  TERARK_DIE("Not Supported platform");
}

} // namespace terark
