#pragma once

#include <terark/config.hpp>
#include <sys/uio.h>

namespace terark {

TERARK_DLL_EXPORT ssize_t easy_readv(int fd, iovec*, int num, int* next_idx);
TERARK_DLL_EXPORT ssize_t easy_writev(int fd, iovec*, int num, int* next_idx);

inline bool iovec_finished(const iovec* iov, int num) {
    return 0 == iov[num-1].iov_len;
}

} // namespace terark
