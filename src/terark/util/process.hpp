//
// Created by leipeng on 2019-06-18.
//

#pragma once

#include <stdio.h>
#include <terark/config.hpp>
#include <terark/io/FileStream.hpp>
#include <terark/util/function.hpp>
#include <thread>
#include <atomic>
#include <memory>
#include <utility>
#include <future>
#include <sys/uio.h> // linux process_vm_readv/process_vm_writev

namespace terark {

TERARK_DLL_EXPORT int system_vfork(const char*);
inline int system_vfork(fstring cmd) { return system_vfork(cmd.c_str()); }
// sig == 0 disables parent-death notification. On Linux, a nonzero sig is
// delivered when the creating thread exits. Only SIGKILL guarantees termination.
// This applies to the directly executed process, not its forked descendants.
// The sig overload returns -1 with errno on startup failure; successful startup
// returns the waitpid status. The old overload retains positive startup errno.
// Non-Linux platforms reject nonzero sig with ENOTSUP. On Linux, nonzero sig
// selects vfork when SubProcHowSpawn=posix, because spawn has no prctl action.
TERARK_DLL_EXPORT int system_vfork(const char* cmd, int sig);
inline int system_vfork(fstring cmd, int sig) {
    return system_vfork(cmd.c_str(), sig);
}

TERARK_DLL_EXPORT
void vfork_cmd(fstring cmd, fstring stdinData,
               function<void(std::string&& stdoutData, const std::exception*)>,
               fstring tmpFilePrefix = "");

TERARK_DLL_EXPORT
void // std_out_err[0] is stdout, std_out_err[1] is stderr
vfork_cmd(fstring cmd, fstring stdinData, std::string std_out_err[2],
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
/// Commands without shell syntax are split on spaces/tabs and executed via PATH.
/// Quotes, expansions and other shell syntax keep the Bash execution path;
/// parent-death notification then applies to Bash, not its forked descendants.
/// xopen returns false with errno for startup errors. A successfully started
/// command's nonzero exit is reported by err_code/xclose, not by xopen.
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
    static_assert(2 * sizeof(std::atomic<short>) == sizeof(int), "keep old ABI size");
    alignas(int) std::atomic<short> m_child_step;
    std::atomic<short> m_pipe_closed;
    intptr_t m_childpid;
    std::string m_cmd;
    std::unique_ptr<std::thread> m_thr;

    struct Exec;
    friend struct VforkCmdImpl;
    // Keep the original exported entry point and the object layout for old ABI.
    void vfork_exec_wait() noexcept;
    void vfork_exec_wait(Exec&) noexcept;
    bool xopen_impl(fstring cmd, fstring mode,
                    function<void(ProcPipeStream*)> onFinish,
                    int sig, int stdout_fd, int stderr_fd) noexcept;
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
    // The new overloads preserve all existing exported signatures. sig has no
    // default argument, so existing calls continue to select the old overloads.
    ProcPipeStream(fstring cmd, fstring mode, int sig);
    ProcPipeStream(fstring cmd, fstring mode,
                   function<void(ProcPipeStream*)> onFinish, int sig);
    void open(fstring cmd, fstring mode, int sig);
    bool xopen(fstring cmd, fstring mode, int sig) noexcept;
    void open(fstring cmd, fstring mode,
              function<void(ProcPipeStream*)> onFinish, int sig);
    bool xopen(fstring cmd, fstring mode,
               function<void(ProcPipeStream*)> onFinish, int sig) noexcept;
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
void vfork_cmd(fstring cmd,
               function<void(ProcPipeStream&)> write,
               function<void(std::string&& stdoutData,
                             std::string&& stderrData,
                             const std::exception*)> onFinish,
               fstring tmpFilePrefix = "");

TERARK_DLL_EXPORT
std::future<std::string>
vfork_cmd(fstring cmd, function<void(ProcPipeStream&)> write,
          fstring tmpFilePrefix = "");

// The sig overloads use the same arguments as the old overloads, followed by
// the parent-death signal. Pass an empty tmpFilePrefix to use the default path.
// The array-output overload throws on execution failure, and the future
// overloads deliver failures through future::get().
TERARK_DLL_EXPORT
void vfork_cmd(fstring cmd, fstring stdinData,
               function<void(std::string&&, const std::exception*)> onFinish,
               fstring tmpFilePrefix, int sig);
TERARK_DLL_EXPORT
void vfork_cmd(fstring cmd, fstring stdinData, std::string std_out_err[2],
               fstring tmpFilePrefix, int sig);
TERARK_DLL_EXPORT
std::future<std::string>
vfork_cmd(fstring cmd, fstring stdinData, fstring tmpFilePrefix, int sig);
TERARK_DLL_EXPORT
void vfork_cmd(fstring cmd, function<void(ProcPipeStream&)> write,
               function<void(std::string&&, const std::exception*)> onFinish,
               fstring tmpFilePrefix, int sig);
TERARK_DLL_EXPORT
void vfork_cmd(fstring cmd, function<void(ProcPipeStream&)> write,
               function<void(std::string&&, std::string&&,
                             const std::exception*)> onFinish,
               fstring tmpFilePrefix, int sig);
TERARK_DLL_EXPORT
std::future<std::string>
vfork_cmd(fstring cmd, function<void(ProcPipeStream&)> write,
          fstring tmpFilePrefix, int sig);

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
