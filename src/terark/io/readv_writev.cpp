#include "readv_writev.hpp"

namespace terark {

// once finished, corresponding iovec will be cleared
TERARK_DLL_EXPORT
ssize_t easy_writev(int fd, iovec* iov, int num, int* next_idx) {
  int idx = *next_idx;
  ssize_t finished = writev(fd, iov + idx, num - idx);
  if (finished > 0) {
    ssize_t concat_beg = 0;
    for (; idx < num; ++idx) {
      ssize_t concat_end = concat_beg + iov[idx].iov_len;
      if (finished < concat_end) {
        ssize_t offset = finished - concat_beg;
        iov[idx].iov_base = offset + (char*)iov->iov_base;
        iov[idx].iov_len -= offset;
        *next_idx = idx;
        break;
      }
      else {
        iov[idx].iov_base = nullptr;
        iov[idx].iov_len = 0;
      }
      concat_beg = concat_end;
    }
  }
  return finished;
}

// once finished, corresponding iovec will be cleared
TERARK_DLL_EXPORT
ssize_t easy_readv(int fd, iovec* iov, int num, int* next_idx) {
  int idx = *next_idx;
  ssize_t finished = readv(fd, iov + idx, num - idx);
  if (finished > 0) {
    ssize_t concat_beg = 0;
    for (; idx < num; ++idx) {
      ssize_t concat_end = concat_beg + iov[idx].iov_len;
      if (finished < concat_end) {
        ssize_t offset = finished - concat_beg;
        iov[idx].iov_base = offset + (char*)iov->iov_base;
        iov[idx].iov_len -= offset;
        *next_idx = idx;
        break;
      }
      else {
        iov[idx].iov_base = nullptr;
        iov[idx].iov_len = 0;
      }
      concat_beg = concat_end;
    }
  }
  return finished;
}

} // namespace terark
