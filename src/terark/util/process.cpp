//
// Created by leipeng on 2019-06-18.
//

#include "process.hpp"
#include <terark/num_to_str.hpp>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <system_error>
#include <signal.h>
#if defined(__linux__)
    #include <sys/prctl.h>
#endif

#if defined(_MSC_VER)
    #define NOMINMAX
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <io.h>
#else
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
    #include <spawn.h>
#endif
#include "stat.hpp"
#include <fcntl.h>

#include <terark/num_to_str.hpp>
#include <terark/util/function.hpp>
#include <terark/util/enum.hpp>

namespace terark {

    static long g_debug = getEnvLong("ProcPipeStreamDebugLevel", TERARK_IF_DEBUG(3, -1));
#if defined(_MSC_VER)
    static bool stderr_is_tty = false;
#else
    static bool stderr_is_tty = isatty(STDERR_FILENO);
#endif

#define LOG_printf(type, format, ...) \
        LOG_printf_ex(type, format, "1;31", ##__VA_ARGS__)
///@param color "1;31": bold red, "1;33": bold yellow, "0;35": normal purple
#define LOG_printf_ex(type, format, color, ...) \
    if (stderr_is_tty) \
        fprintf(stderr, "%s:%d:" type ": \33[" color "m" format \
                "\33[0m\n", TERARK_PP_SmartForPrintf(__FILE__,__LINE__, ##__VA_ARGS__)); \
    else \
        fprintf(stderr, "%s:%d:" type ": " format \
                "\n", TERARK_PP_SmartForPrintf(__FILE__,__LINE__, ##__VA_ARGS__))
#undef DEBUG
#define DEBUG(level, format, ...) \
    if (g_debug >= level) LOG_printf_ex("Debug" #level, format, "0;35", ##__VA_ARGS__); \
    else (void)0
#define ERROR(format, ...) \
    if (g_debug >= 0) LOG_printf("Error", format, ##__VA_ARGS__); \
    else (void)0
//-----------------------------------------------------------------------------

// "/bin/sh" may not support process substitution, use bash
#define SHELL_CMD "/usr/bin/bash"

namespace proc_pipe_detail {
int child_state(const volatile int& state) {
#if defined(_MSC_VER)
    return InterlockedCompareExchange((volatile LONG*)&state, 0, 0);
#else
    return __atomic_load_n(&state, __ATOMIC_ACQUIRE);
#endif
}
void set_child_state(volatile int& state, int bits) {
#if defined(_MSC_VER)
    InterlockedExchange((volatile LONG*)&state, bits);
#else
    __atomic_store_n(&state, bits, __ATOMIC_RELEASE);
#endif
}

#if !defined(_MSC_VER)
int check_signal(int sig) {
    if (sig < 0 || sig >= NSIG)
        return EINVAL;
#if !defined(__linux__)
    if (sig)
        return ENOTSUP;
#endif
    return 0;
}

// Keep all internal descriptors above stderr, including when a caller has
// closed one or more standard descriptors. Every new descriptor starts CLOEXEC.
int make_pipe(int fds[2]) {
#if defined(__linux__)
    if (::pipe2(fds, O_CLOEXEC) < 0)
        return errno;
#else
    if (::pipe(fds) < 0)
        return errno;
#endif
    for (int i = 0; i != 2; ++i) {
        if (fds[i] < 3) {
            int fd = fcntl(fds[i], F_DUPFD_CLOEXEC, 3);
            if (fd < 0) {
                int err = errno;
                ::close(fds[0]);
                ::close(fds[1]);
                fds[0] = fds[1] = -1;
                return err;
            }
            ::close(fds[i]);
            fds[i] = fd;
        }
#if !defined(__linux__)
        if (fcntl(fds[i], F_SETFD, FD_CLOEXEC) < 0) {
            int err = errno;
            ::close(fds[0]);
            ::close(fds[1]);
            fds[0] = fds[1] = -1;
            return err;
        }
#endif
    }
    return 0;
}

pid_t wait_child(pid_t pid, int* status) {
    pid_t ret;
    do {
        ret = waitpid(pid, status, 0);
    } while (ret < 0 && errno == EINTR);
    return ret;
}

bool needs_shell(fstring cmd) {
    for (size_t i = 0; i < cmd.size(); ++i) {
        unsigned char c = cmd[i];
        if (c == 0x60 || strchr("\\'\"$*?[]{}~#!;|&<>()\n", c))
            return true;
    }
    // Assignment words only have shell meaning before the command name.
    size_t i = 0;
    while (i < cmd.size() && (cmd[i] == ' ' || cmd[i] == '\t'))
        ++i;
    if (i < cmd.size() && (cmd[i] == '_' ||
          (cmd[i] >= 'a' && cmd[i] <= 'z') ||
          (cmd[i] >= 'A' && cmd[i] <= 'Z'))) {
        while (++i < cmd.size()) {
            char c = cmd[i];
            if (c == '=')
                return true;
            if (c == '+' && i + 1 < cmd.size() && cmd[i + 1] == '=')
                return true;
            if (!(c == '_' || (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
                break;
        }
    }
    return false;
}

[[noreturn]] void exec_error(int fd, int err) {
    // No stdio, allocation, exception handling or return to the vfork frame.
    while (::write(fd, &err, sizeof(err)) < 0 && errno == EINTR) {}
    _exit(126);
}


struct Command {
    std::string text;
    std::string words;
    std::vector<char*> argv;
    std::vector<std::string> paths;
    std::vector<const char*> path_ptrs;
    int io[3] = { -1, -1, -1 };
    int sig = 0;
    bool shell = false;
    bool merge_stderr = false;

    char* const* argv_data = nullptr;
    const char* const* path_data = nullptr;
    size_t path_count = 0;

    // A separate frame keeps child-only local variables out of the suspended
    // caller's live stack frame. This is the Linux vfork path, not portable POSIX.
    __attribute__((noinline, noreturn))
    void exec_child(int error_fd, pid_t parent, const sigset_t* mask) const {
    #if defined(__linux__)
        if (sig) {
            if (prctl(PR_SET_PDEATHSIG, long(sig), 0L, 0L, 0L) < 0)
                exec_error(error_fd, errno);
            if (getppid() != parent)
                exec_error(error_fd, ESRCH);
        }
    #endif
        for (int i = 0; i != 3; ++i) {
            if (io[i] < 0)
                continue;
            if (shell) {
                // Bash must inherit the descriptors named by /dev/fd redirections.
                if (fcntl(io[i], F_SETFD, 0) < 0)
                    exec_error(error_fd, errno);
            } else {
                if (dup2(io[i], i) < 0)
                    exec_error(error_fd, errno);
                ::close(io[i]);
            }
        }
        if (!shell && merge_stderr && dup2(1, 2) < 0)
            exec_error(error_fd, errno);
        // Caught handlers would be reset by exec anyway. Reset them before
        // unblocking so no inherited handler runs in vfork's shared address space.
        struct sigaction default_action = {};
        default_action.sa_handler = SIG_DFL;
        sigemptyset(&default_action.sa_mask);
        for (int i = 1; i < NSIG; ++i) {
            struct sigaction action;
            // Query the child's own dispositions: the parent's other threads may
            // have installed a handler immediately before fork/vfork.
            if (sigaction(i, nullptr, &action) == 0 &&
                action.sa_handler != SIG_DFL && action.sa_handler != SIG_IGN) {
                if (sigaction(i, &default_action, nullptr) < 0)
                    exec_error(error_fd, errno);
            }
        }
        if (sigprocmask(SIG_SETMASK, mask, nullptr) < 0)
            exec_error(error_fd, errno);
        bool denied = false;
        for (size_t i = 0; i < path_count; ++i) {
            execve(path_data[i], argv_data, environ);
            if (errno == EACCES)
                denied = true;
            else if (errno != ENOENT && errno != ENOTDIR)
                exec_error(error_fd, errno);
        }
        exec_error(error_fd, denied ? EACCES : ENOENT);
    }

    ~Command() { close_io(); }
    void close_io() {
        for (int& fd : io) {
            if (fd >= 0) {
                ::close(fd);
                fd = -1;
            }
        }
    }
    int redirect(int target, int source) {
        int fd = fcntl(source, F_DUPFD_CLOEXEC, 3);
        if (fd < 0)
            return errno;
        if (io[target] >= 0)
            ::close(io[target]);
        io[target] = fd;
        return 0;
    }
    int prepare(fstring cmd, int signal) {
        int err = check_signal(signal);
        if (err)
            return err;
        if (cmd.size() && memchr(cmd.data(), 0, cmd.size()))
            return EINVAL;
        sig = signal;
        text.assign(cmd.size() ? cmd.data() : "", cmd.size());
        shell = needs_shell(cmd);
        return 0;
    }
    void finish() {
        if (shell) {
            // Preserve the old trailing-redirection scope for compound commands.
            const int order[] = { 1, 2, 0 };
            for (int i : order) {
                if (io[i] >= 0) {
                    text += i == 0 ? " < /dev/fd/" :
                            i == 1 ? " > /dev/fd/" : " 2> /dev/fd/";
                    text += std::to_string(io[i]);
                }
            }
            if (merge_stderr)
                text += " 2>&1";
            argv = { const_cast<char*>(SHELL_CMD), const_cast<char*>("-c"),
                     const_cast<char*>(text.c_str()), nullptr };
            paths.emplace_back(SHELL_CMD);
        } else {
            words = text;
            size_t i = 0;
            while (i < words.size()) {
                while (i < words.size() && (words[i] == ' ' || words[i] == '\t'))
                    words[i++] = 0;
                if (i == words.size())
                    break;
                argv.push_back(&words[i]);
                while (i < words.size() && words[i] != ' ' && words[i] != '\t')
                    ++i;
            }
            if (argv.empty())
                return; // An empty command is a successful empty operation.
            argv.push_back(nullptr);
            if (strchr(argv[0], '/')) {
                paths.emplace_back(argv[0]);
            } else {
                const char* path = getenv("PATH");
                std::string default_path;
                if (!path) {
                    size_t size = confstr(_CS_PATH, nullptr, 0);
                    default_path.resize(size ? size : 1);
                    if (size)
                        confstr(_CS_PATH, &default_path[0], size);
                    path = default_path.c_str();
                }
                do {
                    const char* end = strchr(path, ':');
                    size_t size = end ? size_t(end - path) : strlen(path);
                    paths.emplace_back(size ? std::string(path, size) + "/" + argv[0]
                                            : std::string(argv[0]));
                    if (!end)
                        break;
                    path = end + 1;
                } while (true);
            }
        }
        for (const auto& path : paths)
            path_ptrs.push_back(path.c_str());
        argv_data = argv.data();
        path_data = path_ptrs.data();
        path_count = path_ptrs.size();
    }
};

int spawn_posix(Command& cmd, pid_t* pid) {
    posix_spawn_file_actions_t actions;
    int err = posix_spawn_file_actions_init(&actions);
    if (err)
        return err;
    for (int i = 0; i != 3 && !err; ++i) {
        if (cmd.io[i] >= 0) {
            // adddup2(fd, fd) explicitly clears CLOEXEC in the child.
            err = posix_spawn_file_actions_adddup2(&actions, cmd.io[i],
                                                   cmd.shell ? cmd.io[i] : i);
            if (!err && !cmd.shell)
                err = posix_spawn_file_actions_addclose(&actions, cmd.io[i]);
        }
    }
    if (!err && !cmd.shell && cmd.merge_stderr)
        err = posix_spawn_file_actions_adddup2(&actions, 1, 2);
    if (!err)
        err = posix_spawnp(pid, cmd.argv[0], &actions, nullptr, cmd.argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    return err;
}

// Shared fork/vfork setup and startup-error reporting. The caller still
// performs fork/vfork directly and keeps its original waitpid flow.
struct ForkExec {
    int errors[2] = { -1, -1 };
    pid_t parent;
    sigset_t mask;

    int prepare() {
        int err = make_pipe(errors);
        if (err)
            return err;
        parent = getpid();
        sigset_t blocked;
        sigfillset(&blocked);
        err = pthread_sigmask(SIG_SETMASK, &blocked, &mask);
        if (err) {
            ::close(errors[0]);
            ::close(errors[1]);
        }
        return err;
    }
    int finish(pid_t& pid) {
        int err = pid < 0 ? errno : 0;
        pthread_sigmask(SIG_SETMASK, &mask, nullptr);
        ::close(errors[1]);
        if (pid >= 0) {
            int child_error = 0;
            ssize_t n;
            do {
                n = ::read(errors[0], &child_error, sizeof(child_error));
            } while (n < 0 && errno == EINTR);
            if (n > 0)
                err = n == sizeof(child_error) ? child_error : EIO;
            else if (n < 0) {
                err = errno;
                kill(pid, SIGKILL);
            }
            if (err) {
                int status;
                wait_child(pid, &status);
                pid = -1;
            }
        }
        ::close(errors[0]);
        return err;
    }
};
#endif
} // namespace proc_pipe_detail
using namespace proc_pipe_detail;

    // system(cmd) on Linux calling fork which do copy page table
    // we should use vfork
    TERARK_DLL_EXPORT int system_vfork(const char* cmd) {
        int ret = system_vfork(cmd, 0);
        return ret == -1 ? errno : ret; // Keep the old positive-errno convention.
    }

    TERARK_DLL_EXPORT int system_vfork(const char* cmd, int sig) {
    #if defined(_MSC_VER)
        if (sig) {
            errno = ENOTSUP;
            return -1;
        }
        return ::system(cmd); // windows has no fork performance issue
    #else
      try {
        if (!cmd) {
            errno = EINVAL;
            return -1;
        }
        pid_t childpid = -1;
        Command command;
        int err = command.prepare(cmd, sig);
        if (!err) {
            command.finish();
            if (!command.argv.empty()) {
                ForkExec child;
                err = child.prepare();
                if (!err) {
                    childpid = vfork();
                    if (0 == childpid) { // child process
                        command.exec_child(child.errors[1], child.parent, &child.mask);
                    }
                    err = child.finish(childpid);
                }
            }
        }
        if (err) {
            ERROR("spawn(\"%s\") = %s", cmd, strerror(err));
            errno = err;
            return -1;
        }
        int childstatus = 0;
        pid_t pid = childpid < 0 ? childpid : wait_child(childpid, &childstatus);
        if (pid != childpid) {
            int err = errno;
            ERROR("wait \"%s\" = %s", cmd, strerror(err));
            errno = err;
            return -1;
        }
        return childstatus;
      } catch (const std::bad_alloc&) {
        errno = ENOMEM;
      } catch (const std::exception&) {
        errno = EINVAL;
      }
        return -1;
    #endif
    }

/////////////////////////////////////////////////////////////////////////////

#if !defined(_MSC_VER)
struct ProcPipeStream::Exec : Command {};
#endif

ProcPipeStream::ProcPipeStream() noexcept {
    m_pipe[0] = m_pipe[1] = -1;
    m_err = 0;
    m_child_step = 3;
    m_pipe_closed = 0;
    m_childpid = -1;
    m_mode_is_read = false;
}

ProcPipeStream::ProcPipeStream(fstring cmd, fstring mode)
  : ProcPipeStream(cmd, mode, function<void(ProcPipeStream*)>()) {
}

ProcPipeStream::ProcPipeStream(fstring cmd, fstring mode, int sig)
  : ProcPipeStream(cmd, mode, function<void(ProcPipeStream*)>(), sig) {
}

ProcPipeStream::ProcPipeStream(fstring cmd, fstring mode,
                               function<void(ProcPipeStream*)> onFinish) {
    m_pipe[0] = m_pipe[1] = -1;
    m_err = 0;
    m_child_step = 3;
    m_pipe_closed = 0;
    m_childpid = -1;
    m_mode_is_read = false;

    open(cmd, mode, std::move(onFinish));
}

ProcPipeStream::ProcPipeStream(fstring cmd, fstring mode,
                               function<void(ProcPipeStream*)> onFinish, int sig)
  : ProcPipeStream() {
    open(cmd, mode, std::move(onFinish), sig);
}

ProcPipeStream::~ProcPipeStream() {
    if (m_fp) {
        try { close(); } catch (const std::exception&) {}
    }
    if (m_thr) {
        assert(2 == m_child_step);
        assert(!m_thr->joinable());
    }
    else {
        wait_finish();
    }
}

void ProcPipeStream::open(fstring cmd, fstring mode) {
    open(cmd, mode, function<void(ProcPipeStream*)>());
}

void ProcPipeStream::open(fstring cmd, fstring mode, int sig) {
    open(cmd, mode, function<void(ProcPipeStream*)>(), sig);
}

void ProcPipeStream::open(fstring cmd, fstring mode,
                          function<void(ProcPipeStream*)> onFinish) {
    open(cmd, mode, std::move(onFinish), 0);
}

void ProcPipeStream::open(fstring cmd, fstring mode,
                          function<void(ProcPipeStream*)> onFinish, int sig) {
    if (m_fp) {
        THROW_STD(invalid_argument, "File is already open");
    }
    if (!xopen(cmd, mode, std::move(onFinish), sig)) {
        int err = errno;
        THROW_STD(logic_error,
               "cmd = %s, mode = %s, m_err = %d(%s), errno = %d(%s)",
                cmd.c_str(), mode.c_str(),
                m_err, strerror(m_err),
                err, strerror(err)
                );
    }
}

bool ProcPipeStream::xopen(fstring cmd, fstring mode) noexcept {
    return xopen(cmd, mode, function<void(ProcPipeStream*)>());
}

bool ProcPipeStream::xopen(fstring cmd, fstring mode, int sig) noexcept {
    return xopen(cmd, mode, function<void(ProcPipeStream*)>(), sig);
}

bool ProcPipeStream::xopen(fstring cmd, fstring mode,
                           function<void(ProcPipeStream*)> onFinish)
noexcept {
    return xopen(cmd, mode, std::move(onFinish), 0);
}

bool ProcPipeStream::xopen(fstring cmd, fstring mode,
                           function<void(ProcPipeStream*)> onFinish, int sig) noexcept {
    return xopen_impl(cmd, mode, std::move(onFinish), sig, -1, -1);
}

bool ProcPipeStream::xopen_impl(fstring cmd, fstring mode,
                                function<void(ProcPipeStream*)> onFinish,
                                int sig, int stdout_fd, int stderr_fd) noexcept {
#if defined(_MSC_VER)
    errno = ENOTSUP;
    return false;
#else
    if (m_fp) {
        ERROR("%s: File is already open", BOOST_CURRENT_FUNCTION);
        errno = EINVAL;
        return false;
    }
    if (mode.empty() || memchr(mode.data(), 0, mode.size())) {
        errno = EINVAL;
        return false;
    }
    if (mode.strchr('r'))
        m_mode_is_read = true;
    else if (mode.strchr('w') || mode.strchr('a'))
        m_mode_is_read = false;
    else {
        ERROR("%s: mode = \"%s\" is invalid", BOOST_CURRENT_FUNCTION, mode);
        errno = EINVAL; // invalid argument
        return false;
    }

    if (m_thr)
        wait_proc();
    else
        wait_finish();
    m_thr.reset();
    int err = 0;
  try {
    auto exec = std::make_shared<Exec>();
    err = exec->prepare(cmd, sig);
    if (err)
        throw std::system_error(err, std::generic_category());
    if ((err = make_pipe(m_pipe)) != 0) {
        ERROR("pipe() = %s", strerror(err));
        throw std::system_error(err, std::generic_category());
    }
    if (stdout_fd >= 0)
        err = exec->redirect(1, stdout_fd);
    if (!err && stderr_fd >= 0)
        err = exec->redirect(2, stderr_fd);
    if (!err)
        err = exec->redirect(m_mode_is_read ? (mode == "r2" ? 2 : 1) : 0,
                             m_pipe[m_mode_is_read ? 1 : 0]);
    if (err)
        throw std::system_error(err, std::generic_category());
    exec->merge_stderr = mode == "r12";
    exec->finish();
    m_cmd = exec->text;
    DEBUG(4, "mode = %s, m_pipe = [%d, %d], m_cmd = %s", mode, m_pipe[0], m_pipe[1], m_cmd);

    m_fp = fdopen(m_pipe[m_mode_is_read ? 0 : 1],
                  m_mode_is_read ? "r" : mode.strchr('a') ? "a" : "w");
    if (NULL == m_fp) {
        err = errno;
        ERROR("fdopen(\"%s\", \"%s\") = %s", m_cmd, mode, strerror(err));
        throw std::system_error(err, std::generic_category());
    }
    //this->disbuf();
    DEBUG(4, "fdopen(\"%s\") done", m_cmd);
    DEBUG(4, "onFinish is defined = %d", bool(onFinish));
    m_err = 0;
    m_childpid = -1;
    m_child_step = 0;
    m_pipe_closed = 0;
    if (onFinish) {
        // onFinish can own the lifetime of this
        std::thread([this,exec,onFinish=std::move(onFinish)]() {
            this->vfork_exec_wait(*exec);
            if (m_childpid >= 0 || child_state(m_err) == 0) {
                while (!m_pipe_closed)
                    usleep(1000);
                try { onFinish(this); }
                catch (const std::exception& ex) {
                    ERROR("user onFinish thrown: %s", ex.what());
                    set_child_state(m_err, -1);
                }
                catch (...) {
                    ERROR("user onFinish thrown");
                    set_child_state(m_err, -1);
                }
            }
            m_child_step = 3;
        }).detach();
    } else {
        m_thr.reset(new std::thread([this,exec]() {
            this->vfork_exec_wait(*exec);
        }));
    }
    while (m_child_step < 1) {
        usleep(1000); // 1 ms
        //std::this_thread::yield();
    }
    DEBUG(4, "waited m_child_step = %d", m_child_step.load());
    // pid is published once, before step 1. A fast nonzero exit is not a
    // startup failure; an empty command succeeds with pid == -1 and err == 0.
    if (m_childpid >= 0 || child_state(m_err) == 0)
        return true;
    err = child_state(m_err);
    xclose();
    wait_finish();
    m_thr.reset();
  } catch (const std::system_error& ex) {
    err = ex.code().value();
  } catch (const std::bad_alloc&) {
    err = ENOMEM;
  } catch (const std::exception&) {
    err = EINVAL;
  }
    if (m_fp) {
        fclose(m_fp);
        m_fp = nullptr;
        m_pipe[m_mode_is_read ? 0 : 1] = -1;
    }
    for (int& fd : m_pipe) {
        if (fd >= 0)
            ::close(fd);
        fd = -1;
    }
    m_err = err;
    m_child_step = 3;
    m_pipe_closed = 0;
    errno = err;
    return false;
#endif
}

TERARK_ENUM_CLASS(HowSpawn, byte_t, fork, vfork, posix);
// Keep this original (class-exported, although private) symbol for old ABI.
void ProcPipeStream::vfork_exec_wait() noexcept {
#if !defined(_MSC_VER)
    try {
        Exec exec;
        int err = exec.prepare(m_cmd, 0);
        if (!err) {
            exec.finish();
            vfork_exec_wait(exec);
            return;
        }
        __atomic_store_n(&m_err, err, __ATOMIC_RELEASE);
    } catch (const std::exception&) {
        m_err = ENOMEM;
    }
#endif
}

void ProcPipeStream::vfork_exec_wait(Exec& exec) noexcept {
#if !defined(_MSC_VER)
    static const auto how_spawn = enum_value(getenv("SubProcHowSpawn"), HowSpawn::vfork);
    auto how = how_spawn;
    if (how == HowSpawn::posix && exec.sig) {
        DEBUG(1, "PR_SET_PDEATHSIG requires vfork; overriding SubProcHowSpawn=posix");
        how = HowSpawn::vfork;
    }
    pid_t pid = -1;
    int err = 0;
    ForkExec child;
    if (!exec.argv.empty() && how != HowSpawn::posix)
        err = child.prepare();
    if (!err && !exec.argv.empty()) {
        if (how == HowSpawn::vfork) {
            DEBUG(3, "calling vfork, m_cmd = \"%s\"", m_cmd);
            pid = vfork();
            if (0 == pid) { // child process
                exec.exec_child(child.errors[1], child.parent, &child.mask);
            }
            err = child.finish(pid);
            DEBUG(4, "vfork done, childpid = %zd", intptr_t(pid));
        } else if (how == HowSpawn::fork) {
            DEBUG(4, "calling fork, m_cmd = \"%s\"", m_cmd);
            pid = fork();
            if (0 == pid) { // child process
                exec.exec_child(child.errors[1], child.parent, &child.mask);
            }
            err = child.finish(pid);
            DEBUG(4, "fork done, childpid = %zd", intptr_t(pid));
        } else {
            DEBUG(4, "calling posix_spawn, m_cmd = \"%s\"", m_cmd);
            err = spawn_posix(exec, &pid);
            DEBUG(4, "posix_spawn done, childpid = %zd", intptr_t(pid));
        }
    }
    exec.close_io();
    m_childpid = pid;
    // The parent must not retain the child's pipe end while waiting.
    if (m_mode_is_read) {
        DEBUG(4, "parent is read(fd=%d), close write peer(fd=%d)", m_pipe[0], m_pipe[1]);
        ::close(m_pipe[1]); m_pipe[1] = -1;
    } else {
        DEBUG(4, "parent is write(fd=%d), close read peer(fd=%d)", m_pipe[1], m_pipe[0]);
        ::close(m_pipe[0]); m_pipe[0] = -1;
    }
    if (err) {
        ERROR("spawn(\"%s\") = %s", m_cmd, strerror(err));
        __atomic_store_n(&m_err, err, __ATOMIC_RELEASE);
        m_child_step = 2; // startup failed, nothing to wait
        return;
    }
    DEBUG(4, "spawn done, childpid = %zd", m_childpid);
    m_child_step = 1;
    int childstatus = 0;
    if (pid >= 0 && wait_child(pid, &childstatus) != pid) {
        childstatus = errno;
        ERROR("wait \"%s\" = %s", m_cmd, strerror(childstatus));
    }
    DEBUG(4, "child proc done, childstatus = %d ++++", childstatus);
    __atomic_store_n(&m_err, childstatus, __ATOMIC_RELEASE);
    m_child_step = 2;
#endif
}

void ProcPipeStream::close() {
    xclose();
}

int ProcPipeStream::xclose() noexcept {
    DEBUG(4, "xclose(): m_pipe = [%d, %d]", m_pipe[0], m_pipe[1]);
    if (m_fp) {
        close_pipe();
        if (m_thr) {
            wait_proc();
        }
    }
    return child_state(m_err);
}

void ProcPipeStream::close_pipe() noexcept {
    DEBUG(4, "close_pipe(): m_pipe = [%d, %d]", m_pipe[0], m_pipe[1]);
    assert(m_pipe[0] >= 0 || m_pipe[1] >= 0);
    DEBUG(4, "doing fclose(fd=%d)", m_pipe[m_mode_is_read?0:1]);
    assert(NULL != m_fp);
    fclose(m_fp);
    m_fp = NULL;
    DEBUG(4, "done  fclose(fd=%d)", m_pipe[m_mode_is_read?0:1]);
    m_pipe[m_mode_is_read?0:1] = -1;
    m_pipe_closed = 1;
}

void ProcPipeStream::wait_proc() noexcept {
    if (!m_thr || !m_thr->joinable())
        return;
    intptr_t waited_ms = 0;
    while (m_child_step < 2) {
        TERARK_IF_MSVC(Sleep(10), usleep(10000)); // 10 ms
        waited_ms += 10;
        if (waited_ms % 5000 == 0) {
            DEBUG(3, "waited_ms = %zd", waited_ms);
        }
    }
    assert(m_thr != nullptr);
    assert(m_thr->joinable());
    m_thr->join();
    DEBUG(4, "m_thr joined\n");
}

void ProcPipeStream::wait_finish() noexcept {
    if (m_thr) {
        wait_proc();
        return;
    }
    size_t waited_ms = 0;
    while (m_child_step < 3) {
        TERARK_IF_MSVC(Sleep(10), usleep(10000)); // 10 ms
        waited_ms += 10;
        if (waited_ms % 5000 == 0) {
            DEBUG(3, "m_child_step = %d, wait onFinish = %zd", m_child_step.load(), waited_ms);
        }
    }
}

#define ProcPipeStream_PREVENT_UNEXPECTED_FILE_DELET 1

struct VforkCmdPromise {
    std::promise<std::string> promise;
    std::exception_ptr failure;
    std::string result;

    void operator()(std::string&& stdoutData, const std::exception* ex) {
        DEBUG(4, "VforkCmdPromise.set(%s)\n", stdoutData);
        if (ex) {
            try {
                throw std::runtime_error(ex->what());
            } catch (...) {
                failure = std::current_exception();
            }
        }
        else {
          result = std::move(stdoutData);
        }
    }
    std::future<std::string> get_future() {
        // Fulfill only after both the writer and onFinish have completed.
        auto future = promise.get_future();
        if (failure)
            promise.set_exception(failure);
        else
            promise.set_value(std::move(result));
        return future;
    }
};

struct VforkCmdImpl {
    std::string tmp_file;
    std::string tmp_file_stderr;
    string_appender<> cmdw;
    int fd = -1;
    int fd_stderr = -1;
    ProcPipeStream proc;

    VforkCmdImpl(fstring cmd, fstring tmpFilePrefix) {
      try {
        if (tmpFilePrefix.empty()) {
            tmpFilePrefix = "/tmp/ProcPipeStream-";
        }
        tmp_file = tmpFilePrefix + "XXXXXX";
#if defined(__linux__)
        fd = mkostemp(&tmp_file[0], O_CLOEXEC);
#else
        fd = mkstemp(&tmp_file[0]);
#endif
        if (fd < 0) {
            THROW_STD(runtime_error, "mkstemp(%s) = %s", tmp_file.c_str(),
                      strerror(errno));
        }
        tmp_file_stderr = tmp_file + ".err";
        fd_stderr = ::open(tmp_file_stderr.c_str(),
                           O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC, 0600);
        if (fd_stderr < 0) {
            THROW_STD(runtime_error, "open(%s, O_CREAT|O_EXCL|O_RDWR, 0600) = %s",
                      tmp_file_stderr.c_str(), strerror(errno));
        }
        cmdw.reserve(cmd.size() + 32);
        cmdw << cmd;
        if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 ||
            fcntl(fd_stderr, F_SETFD, FD_CLOEXEC) < 0) {
            int err = errno;
            THROW_STD(runtime_error, "fcntl(FD_CLOEXEC) = %s", strerror(err));
        }
      } catch (...) {
        // A failed open did not create a file we own. In particular, never
        // remove an existing stderr path after O_EXCL reports a collision.
        if (fd < 0)
            tmp_file.clear();
        if (fd_stderr < 0)
            tmp_file_stderr.clear();
        close_files();
        throw;
      }
    }

    void open(fstring cmd, function<void(ProcPipeStream*)> onFinish, int sig) {
        if (!proc.xopen_impl(cmd, "w", std::move(onFinish), sig, fd, fd_stderr)) {
            int err = errno;
            THROW_STD(runtime_error, "vfork_cmd startup failed: %s", strerror(err));
        }
    }

    ~VforkCmdImpl() { close_files(); }

    void close_files() noexcept {
        if (fd >= 0) {
            ::close(fd);
            fd = -1;
        }
        if (!tmp_file.empty()) {
            ::remove(tmp_file.c_str());
        }
        if (fd_stderr >= 0) {
            ::close(fd_stderr);
            fd_stderr = -1;
        }
        if (!tmp_file_stderr.empty()) {
            ::remove(tmp_file_stderr.c_str());
        }
    }

    std::string read_stdout() {
        return read_output(this->tmp_file, this->fd);
    }
    std::string read_stderr() {
        return read_output(this->tmp_file_stderr, this->fd_stderr);
    }
    static
    std::string read_output(const std::string& tmp_file, int fd) {
        //
        // now cmd sub process must have finished
        //
#if ProcPipeStream_PREVENT_UNEXPECTED_FILE_DELET
        ::lseek(fd, 0, SEEK_SET); // must lseek to begin
#else
        fd = ::open(tmp_file.c_str(), O_RDONLY);
        if (fd < 0) {
          THROW_STD(runtime_error, "::open(fname=%s, O_RDONLY) = %s",
                    tmp_file.c_str(), strerror(errno));
        }
#endif
        struct ll_stat st;
        if (::ll_fstat(fd, &st) < 0) {
            THROW_STD(runtime_error, "::fstat(fname=%s) = %s",
                      tmp_file.c_str(), strerror(errno));
        }
        std::string result;
        if (st.st_size) {
            result.resize(st.st_size);
            intptr_t rdlen = ::read(fd, &*result.begin(), st.st_size);
            if (intptr_t(st.st_size) != rdlen) {
                THROW_STD(runtime_error,
                          "::read(fname=%s, len=%zd) = %zd : %s",
                          tmp_file.c_str(),
                          size_t(st.st_size), size_t(rdlen),
                          strerror(errno));
            }
        }
        return result;
    }
};

void
vfork_cmd(fstring cmd, fstring stdinData,
          function<void(std::string&&, const std::exception*)> onFinish,
          fstring tmpFilePrefix)
{
    vfork_cmd(cmd, stdinData, std::move(onFinish), tmpFilePrefix, 0);
}

void
vfork_cmd(fstring cmd, fstring stdinData,
          function<void(std::string&&, const std::exception*)> onFinish,
          fstring tmpFilePrefix, int sig)
{
    auto writeStdinData = [stdinData](ProcPipeStream& pipe) {
       pipe.ensureWrite(stdinData.data(), stdinData.size());
    };
    vfork_cmd(cmd, ref(writeStdinData), onFinish, tmpFilePrefix, sig);
}

TERARK_DLL_EXPORT
void vfork_cmd(fstring cmd,
               function<void(ProcPipeStream&)> write,
               function<void(std::string&& stdoutData, const std::exception*)> onFinish,
               fstring tmpFilePrefix)
{
    vfork_cmd(cmd, std::move(write), std::move(onFinish), tmpFilePrefix, 0);
}

TERARK_DLL_EXPORT
void vfork_cmd(fstring cmd,
               function<void(ProcPipeStream&)> write,
               function<void(std::string&& stdoutData, const std::exception*)> onFinish,
               fstring tmpFilePrefix, int sig)
{
    vfork_cmd(cmd, std::move(write),
        [onFinish=std::move(onFinish)](std::string&& stdoutData,
                                       std::string&& /*stderrData*/,
                                       const std::exception* ex) {
            onFinish(std::move(stdoutData), ex);
        },
        tmpFilePrefix, sig);
}

TERARK_DLL_EXPORT
void vfork_cmd(fstring cmd,
               function<void(ProcPipeStream&)> write,
               function<void(std::string&& stdoutData,
                             std::string&& stderrData,
                             const std::exception*)> onFinish,
               fstring tmpFilePrefix)
{
    vfork_cmd(cmd, std::move(write), std::move(onFinish), tmpFilePrefix, 0);
}

TERARK_DLL_EXPORT
void vfork_cmd(fstring cmd,
               function<void(ProcPipeStream&)> write,
               function<void(std::string&& stdoutData,
                             std::string&& stderrData,
                             const std::exception*)> onFinish,
               fstring tmpFilePrefix, int sig)
{
    auto share = std::make_shared<VforkCmdImpl>(cmd, tmpFilePrefix);

    share->open(cmd,
   [share, onFinish=std::move(onFinish)](ProcPipeStream* proc) {
        std::string stdoutData, stderrData;
        std::exception_ptr failure;
        try {
            stdoutData = share->read_stdout();
            stderrData = share->read_stderr();
            if (proc->err_code() != 0) {
                string_appender<> msg;
                msg << "vfork_cmd error: ";
                msg << "realcmd = " << share->cmdw << ", ";
                msg << "err_code/childstatus = " << proc->err_code();
                msg^"(0x%X)"^proc->err_code();
                std::runtime_error ex(msg);
                failure = std::make_exception_ptr(ex);
            }
        }
        catch (...) {
            failure = std::current_exception();
        }
        // Invoke the callback once, even when it throws.
        if (failure) {
            try {
                std::rethrow_exception(failure);
            } catch (const std::exception& ex) {
                onFinish(std::move(stdoutData), std::move(stderrData), &ex);
            }
        }
        else {
            onFinish(std::move(stdoutData), std::move(stderrData), nullptr);
        }
    }, sig);
    try {
        if (write) {
            write(share->proc);
        }
    } catch (...) {
        share->proc.close();
        share->proc.wait_finish();
        throw;
    }
    share->proc.close();
    share->proc.wait_finish();
}

TERARK_DLL_EXPORT
void // std_out_err[0] is stdout, std_out_err[1] is stderr
vfork_cmd(fstring cmd, fstring stdinData, std::string std_out_err[2],
          fstring tmpFilePrefix) {
    vfork_cmd(cmd, stdinData, std_out_err, tmpFilePrefix, 0);
}

TERARK_DLL_EXPORT
void // std_out_err[0] is stdout, std_out_err[1] is stderr
vfork_cmd(fstring cmd, fstring stdinData, std::string std_out_err[2],
          fstring tmpFilePrefix, int sig) {
    auto writeStdinData = [stdinData](ProcPipeStream& pipe) {
       pipe.ensureWrite(stdinData.data(), stdinData.size());
    };
    std::exception_ptr failure;
    vfork_cmd(cmd, writeStdinData,
        [&](std::string&& stdoutData,
            std::string&& stderrData,
            const std::exception* ex)
        {
            std_out_err[0] = std::move(stdoutData);
            std_out_err[1] = std::move(stderrData);
            if (ex) {
                try {
                    throw std::runtime_error(ex->what());
                } catch (...) {
                    failure = std::current_exception();
                }
            }
        },
        tmpFilePrefix, sig);
    if (failure)
        std::rethrow_exception(failure);
}

TERARK_DLL_EXPORT
std::future<std::string>
vfork_cmd(fstring cmd, fstring stdinData, fstring tmpFilePrefix) {
    return vfork_cmd(cmd, stdinData, tmpFilePrefix, 0);
}

TERARK_DLL_EXPORT
std::future<std::string>
vfork_cmd(fstring cmd, fstring stdinData, fstring tmpFilePrefix, int sig) {
    auto writeStdinData = [stdinData](ProcPipeStream& pipe) {
       pipe.ensureWrite(stdinData.data(), stdinData.size());
    };
    return vfork_cmd(cmd, ref(writeStdinData), tmpFilePrefix, sig);
}

TERARK_DLL_EXPORT
std::future<std::string>
vfork_cmd(fstring cmd, function<void(ProcPipeStream&)> write,
          fstring tmpFilePrefix) {
    return vfork_cmd(cmd, std::move(write), tmpFilePrefix, 0);
}

TERARK_DLL_EXPORT
std::future<std::string>
vfork_cmd(fstring cmd, function<void(ProcPipeStream&)> write,
          fstring tmpFilePrefix, int sig) {
    VforkCmdPromise prom;
    try {
        vfork_cmd(cmd, std::move(write), ref(prom), tmpFilePrefix, sig);
    } catch (...) {
        prom.failure = std::current_exception();
    }
    return prom.get_future();
}

///////////////////////////////////////////////////////////////////////////

bool process_mem_read(pid_t pid, void* data, size_t len, size_t r_addr) {
  iovec local, remote;
  local.iov_base = data;
  local.iov_len = len;
  remote.iov_base = (void*)r_addr;
  remote.iov_len = len;
  ssize_t n_read = process_vm_readv(pid, &local, 1, &remote, 1, 0);
  /*
  if (size_t(n_read) != len) {
    TERARK_DIE("process_read(%d, %p, %zd, %zd) = (n_read=%zd) : %m", pid, data, len, r_addr, n_read);
  }
  */
  return size_t(n_read) == len;
}
bool process_mem_write(pid_t pid, const void* data, size_t len, size_t r_addr) {
  iovec local, remote;
  local.iov_base = (void*)data;
  local.iov_len = len;
  remote.iov_base = (void*)r_addr;
  remote.iov_len = len;
  ssize_t n_write = process_vm_writev(pid, &local, 1, &remote, 1, 0);
  /*
  if (size_t(n_write) != len) {
    TERARK_DIE("process_write(%d, %p, %zd, %zd) = (n_write=%zd) : %m", pid, data, len, r_addr, n_write);
  }
  */
  return size_t(n_write) == len;
}

} // namespace terark
