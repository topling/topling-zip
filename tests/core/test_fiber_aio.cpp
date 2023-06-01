#include <terark/thread/fiber_aio.hpp>
#include <terark/fstring.hpp>
#include <terark/thread/fiber_yield.hpp>
#include <terark/util/atomic.hpp>
#include <terark/util/profiling.hpp>
#include <boost/fiber/fiber.hpp>
#include <boost/fiber/protected_fixedsize_stack.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <random>
#include <thread>
#include <vector>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main() {
  #if defined(__DARWIN_NULL)
    return 0;
  #endif
    const char* fname = "fiber_aio.test.bin";
    int flags = O_CLOEXEC|O_CREAT|O_RDWR;
  #if defined(O_DIRECT)
    flags |= O_DIRECT;
  #endif
    int fd = open(fname, flags, 0600);
    if (fd < 0) {
        fprintf(stderr, "ERROR: open(%s) = %s\n", fname, strerror(errno));
        return 1;
    }
  #if defined(F_NOCACHE)
    if (fcntl(fd, F_SETFD, flags|F_NOCACHE) < 0) {
        fprintf(stderr, "ERROR: fcntl(%d -> '%s') = %s\n", fd, fname, strerror(errno));
        return 1;
    }
  #endif
    using namespace terark;
    const intptr_t FileSize = ParseSizeXiB(getenv("FileSize"), 2L << 20); // default 2M
    const intptr_t WriteSize = ParseSizeXiB(getenv("WriteSize"), FileSize*10);
    const intptr_t ReadSize = ParseSizeXiB(getenv("ReadSize"), FileSize*10);
    const intptr_t Threads = getEnvLong("Threads", 4);
    const intptr_t BlockSize = ParseSizeXiB(getenv("BlockSize"), 4096);
    if (BlockSize < 512 || (BlockSize & (BlockSize-1)) != 0) {
        fprintf(stderr, "Invalid BlockSize = %zd\n", BlockSize);
        return 1;
    }
    if (Threads <= 0) {
        fprintf(stderr, "Invalid Threads = %zd\n", Threads);
        return 1;
    }
    fprintf(stderr, "Threads=%zd, BlockSize=%zd, FileSize=%zd, ReadSize=%zd, WriteSize=%zd\n",
        Threads, BlockSize, FileSize, ReadSize, WriteSize);
    ftruncate(fd, FileSize);
    intptr_t allsum = 0;
    auto thr_fun = [&](intptr_t tno) {
        void* buf = NULL;
        int err = posix_memalign(&buf, BlockSize, BlockSize);
        if (err) {
            fprintf(stderr,
                "ERROR: tno = %zd, posix_memalign(%zd) = %s\n",
                tno, BlockSize, strerror(err));
            exit(1);
        }
        std::mt19937_64 rand;
        intptr_t sum = 0;
        while (sum < WriteSize / Threads) {
            intptr_t offset = rand() % FileSize & -BlockSize;
            for (intptr_t i = 0; i < BlockSize; i += 16) {
                sprintf((char*)buf + i, "%16llX", (long long)rand());
            }
          #if 0
            #define DO_WRITE fiber_put_write
          #else
            #define DO_WRITE fiber_aio_write
          #endif
            intptr_t n = DO_WRITE(fd, buf, BlockSize, offset);
            if (n != BlockSize) {
                fprintf(stderr,
                    "ERROR: " BOOST_PP_STRINGIZE(DO_WRITE) "(offset = %zd, len = %zd) = %zd : %s\n",
                    offset, BlockSize, n, strerror(errno));
                exit(1);
            }
            sum += BlockSize;
        }
        as_atomic(allsum) += sum;
    };
    std::vector<std::thread> tv;
    profiling pf;
    fprintf(stderr, "testing " BOOST_PP_STRINGIZE(DO_WRITE) "...\n");
    long long t0 = pf.now();
    for (intptr_t tno = 0; tno < Threads; ++tno) {
        tv.emplace_back(thr_fun, tno);
    }
    for (std::thread& t : tv) {
        t.join();
    }
    long long t1 = pf.now();

    fprintf(stderr, "wr time = %8.3f sec\n", pf.sf(t0,t1));
    fprintf(stderr, "wr iops = %8.3f K\n", WriteSize/BlockSize/pf.mf(t0,t1));
    fprintf(stderr, "wr iobw = %8.3f GiB\n", WriteSize/pf.sf(t0,t1)/(1L<<30));

    //---------------------- fiber_aio_read -----------------
    fprintf(stderr, "testing fiber_aio_read...\n");
    std::mt19937_64 rnd;
    auto read_fun = [&](intptr_t fb_id) {
        void *buf1 = NULL, *buf2 = NULL;
        int err = posix_memalign(&buf1, BlockSize, BlockSize);
        if (err) {
            fprintf(stderr,
                "ERROR: fb_id = %zd, posix_memalign(%zd) = %s\n",
                fb_id, BlockSize, strerror(err));
            exit(1);
        }
        err = posix_memalign(&buf2, BlockSize, BlockSize);
        if (err) {
            fprintf(stderr,
                "ERROR: fb_id = %zd, posix_memalign(%zd) = %s\n",
                fb_id, BlockSize, strerror(err));
            exit(1);
        }
        intptr_t sum = 0;
        while (sum < ReadSize / Threads) {
            intptr_t offset = rnd() % FileSize & -BlockSize;
            intptr_t n1 = fiber_aio_read(fd, buf1, BlockSize, offset);
            if (n1 != BlockSize) {
                fprintf(stderr,
                    "ERROR: fiber_aio_read(offset = %zd, len = %zd) = %zd : %s\n",
                    offset, BlockSize, n1, strerror(errno));
                exit(1);
            }
            intptr_t n2 = pread(fd, buf2, BlockSize, offset);
            if (n2 != BlockSize) {
                fprintf(stderr,
                    "ERROR: pread(offset = %zd, len = %zd) = %zd : %s\n",
                    offset, BlockSize, n2, strerror(errno));
                exit(1);
            }
            if (memcmp(buf1, buf2, BlockSize) != 0) {
                fprintf(stderr,
                    "ERROR: offset = %zd, len = %zd, pread & fiber_aio_read result different\n",
                    offset, BlockSize);
                exit(1);
            }
            sum += BlockSize;
        }
        as_atomic(allsum) += sum;
    };
    std::vector<boost::fibers::fiber> fb_vec;
    for (intptr_t i = 0; i < Threads; i++) {
        size_t stack_size = 128 * 1024;
        fb_vec.emplace_back(std::allocator_arg_t(),
            boost::fibers::protected_fixedsize_stack(stack_size), read_fun, i);
    }
    for (intptr_t i = 0; i < Threads; i++) {
        fb_vec[i].join();
    }
    long long t2 = pf.now();

    fprintf(stderr, "rd time = %8.3f sec\n", pf.sf(t1,t2));
    fprintf(stderr, "rd iops = %8.3f K\n", ReadSize/BlockSize/pf.mf(t1,t2));
    fprintf(stderr, "rd iobw = %8.3f GiB\n", ReadSize/pf.sf(t1,t2)/(1L<<30));

    close(fd);
    remove(fname);
    return 0;
}
