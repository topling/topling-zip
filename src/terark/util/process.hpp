//
// Created by leipeng on 2019-06-18.
//

#pragma once

#include <stdio.h>
#include <terark/config.hpp>
#include <terark/io/FileStream.hpp>
#include <terark/util/function.hpp>
#include <thread>
#include <memory>
#include <utility>
#include <future>
#include <sys/uio.h> // linux process_vm_readv/process_vm_writev

namespace terark {

TERARK_DLL_EXPORT int system_vfork(const char*);
inline int system_vfork(fstring cmd) { return system_vfork(cmd.c_str()); }

TERARK_DLL_EXPORT
void vfork_cmd(fstring cmd, fstring stdinData,
               function<void(std::string&& stdoutData, const std::exception*)>,
               fstring tmpFilePrefix = "");

TERARK_DLL_EXPORT
std::future<std::string>
vfork_cmd(fstring cmd, fstring stdinData, fstring tmpFilePrefix = "");

/// Notes:
///   1. If mode = "r", then stdout redirect  in @param cmd is not allowed
///   2. If mode = "w", then stdin  redirect  in @param cmd is not allowed
///   3. stderr redirect such as 2>&1 or 1>&2 in @param cmd is not allowed
///   4. If redirect rules are violated, the behavior is undefined
///   5. If you needs redirect, write a wrapping shell script
class TERARK_DLL_EXPORT ProcPipeStream : public FileStream {
    using FileStream::dopen;
    using FileStream::size;
    using FileStream::attach;
    using FileStream::detach;
    using FileStream::rewind;
    using FileStream::seek;
    using FileStream::chsize;
    using FileStream::fsize;
    using FileStream::pread;
    using FileStream::pwrite;
    using FileStream::tell;

    int m_pipe[2];
    int m_err;
    bool m_mode_is_read;
    volatile int m_child_step;
    intptr_t m_childpid;
    std::string m_cmd;
    std::unique_ptr<std::thread> m_thr;

    void vfork_exec_wait() noexcept;
    void close_pipe() noexcept;
    void wait_proc() noexcept;

public:
    ProcPipeStream() noexcept;
    ProcPipeStream(fstring cmd, fstring mode);
    ~ProcPipeStream();
    void open(fstring cmd, fstring mode);
    bool xopen(fstring cmd, fstring mode) noexcept;

    ///@{
    ///@param onFinish called after sub proc finished
    ///@note close/xclose must be called before onFinish
    ///@note close/xclose can not be called in onFinish
    ProcPipeStream(fstring cmd, fstring mode, function<void(ProcPipeStream*)> onFinish);
    void open(fstring cmd, fstring mode, function<void(ProcPipeStream*)> onFinish);
    bool xopen(fstring cmd, fstring mode, function<void(ProcPipeStream*)> onFinish) noexcept;
    void wait_finish() noexcept;
    ///@}

    void close();
    int xclose() noexcept;
    int err_code() const noexcept { return m_err; }
};

TERARK_DLL_EXPORT
void vfork_cmd(fstring cmd,
               function<void(ProcPipeStream&)> write,
               function<void(std::string&& stdoutData, const std::exception*)>,
               fstring tmpFilePrefix = "");

TERARK_DLL_EXPORT
std::future<std::string>
vfork_cmd(fstring cmd, function<void(ProcPipeStream&)> write,
          fstring tmpFilePrefix = "");

//
// these functions are for easy use, so the return value are narrowed to bool
// if you want more accurate control, use process_vm_readv/process_vm_writev
//

bool process_mem_read(pid_t pid, void* data, size_t len, size_t r_addr);
bool process_mem_write(pid_t pid, const void* data, size_t len, size_t r_addr);

inline bool process_mem_read(pid_t pid, void* data, size_t len) {
    // in such case pid is a child process fork'ed by me
    return process_mem_read(pid, data, len, size_t(data));
}
inline bool process_mem_write(pid_t pid, const void* data, size_t len) {
    // in such case pid is a child process fork'ed by me
    return process_mem_write(pid, data, len, size_t(data));
}

template<class... ArgList>
bool process_obj_read(pid_t pid, ArgList&... args) {
    struct iovec iov[] = {{&args,sizeof(args)}...};
    size_t to_read = 0;
    for (size_t i = 0; i < sizeof...(ArgList); ++i) {
        to_read += iov[i].iov_len;
    }
    ssize_t n_read = process_vm_readv(pid, iov, sizeof...(ArgList),
                                      iov, sizeof...(ArgList), 0);
/*
    if (size_t(n_read) != to_read) {
        TERARK_DIE("process_obj_read(pid=%d, to_read=%zd, n_read=%zd) = %m",
                   pid, to_read, n_read);
    }
*/
    return size_t(n_read) == to_read;
}

template<class... ArgList>
bool process_obj_write(pid_t pid, const ArgList&... args) {
    struct iovec iov[] = {{(void*)&args,sizeof(args)}...};
    size_t to_write = 0;
    for (size_t i = 0; i < sizeof...(ArgList); ++i) {
        to_write += iov[i].iov_len;
    }
    ssize_t n_write = process_vm_writev(pid, iov, sizeof...(ArgList),
                                        iov, sizeof...(ArgList), 0);
/*
    if (size_t(n_write) != to_write) {
        TERARK_DIE("process_obj_write(pid=%d, to_write=%zd, n_write=%zd) = %m",
                   pid, to_write, n_write);
    }
*/
    return size_t(n_write) == to_write;
}

} // namespace terark
