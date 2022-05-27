#pragma once
// this file should only be #include in .c/cc/cpp files
#include <linux/futex.h>
#include <sys/syscall.h>   /* For SYS_xxx definitions */
#include <sys/time.h>

inline long
futex(void* uaddr, uint32_t op, uint32_t val, const timespec* timeout = NULL,
      void* uaddr2 = NULL, uint32_t val3 = 0) {
  return syscall(SYS_futex, uaddr, (unsigned long)op, (unsigned long)val,
                 timeout, uaddr2, (unsigned long)val3);
}

inline long
futex(void* uaddr, uint32_t op, uint32_t val, uint32_t val2,
      void* uaddr2 = NULL, uint32_t val3 = 0) {
  return syscall(SYS_futex, uaddr, (unsigned long)op, (unsigned long)val,
                 val2, uaddr2, (unsigned long)val3);
}
