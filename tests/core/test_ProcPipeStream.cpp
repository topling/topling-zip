#include <terark/util/process.hpp>
#include <terark/util/linebuf.hpp>


#include <errno.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace terark;

static std::string capture(fstring cmd, int sig = 0) {
    ProcPipeStream pp(cmd, "r", sig);
    LineBuf line;
    line.read_all(pp);
    TERARK_VERIFY_EQ(pp.xclose(), 0);
    return std::string(line.p, line.n);
}

static void test_parent_death(const std::string& self) {
    int old_subreaper = 0;
    TERARK_VERIFY_EQ(prctl(PR_GET_CHILD_SUBREAPER, &old_subreaper), 0);
    TERARK_VERIFY_EQ(prctl(PR_SET_CHILD_SUBREAPER, 1), 0);
    int fd[2];
    TERARK_VERIFY_EQ(pipe(fd), 0);
    std::string report_fd = std::to_string(fd[1]);
    pid_t owner = fork();
    TERARK_VERIFY_GE(owner, 0);
    if (owner == 0) {
        close(fd[0]);
        execl(self.c_str(), self.c_str(), "--owner", report_fd.c_str(), nullptr);
        _exit(127);
    }
    close(fd[1]);
    pid_t child = -1;
    TERARK_VERIFY_EQ(read(fd[0], &child, sizeof(child)), sizeof(child));
    close(fd[0]);
    TERARK_VERIFY_GT(child, 0);
    TERARK_VERIFY_EQ(kill(owner, SIGKILL), 0);
    int status = 0;
    TERARK_VERIFY_EQ(waitpid(owner, &status, 0), owner);
    pid_t waited = 0;
    for (int i = 0; i < 300 && waited == 0; ++i) {
        waited = waitpid(child, &status, WNOHANG);
        usleep(10000);
    }
    if (waited != child) {
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
    }
    TERARK_VERIFY_EQ(waited, child);
    TERARK_VERIFY(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    TERARK_VERIFY_EQ(prctl(PR_SET_CHILD_SUBREAPER, old_subreaper), 0);
}

static void test_direct_process(const std::string& self) {
    char directory[] = "/tmp/process-test-XXXXXX";
    TERARK_VERIFY(mkdtemp(directory) != nullptr);
    std::string env_file = std::string(directory) + "/env";
    std::string marker = std::string(directory) + "/shell";
    {
        FileStream fp(env_file, "w");
        fprintf(fp, "echo shell > %s\n", marker.c_str());
    }
    const char* old_env = getenv("BASH_ENV");
    bool had_env = old_env != nullptr;
    std::string saved_env = old_env ? old_env : "";
    setenv("BASH_ENV", env_file.c_str(), 1);

    TERARK_VERIFY(capture("printf plain") == "plain");
    TERARK_VERIFY(capture(" \tprintf  %s alpha\t") == "alpha");
    TERARK_VERIFY_EQ(system_vfork("/bin/true", 0), 0);
    TERARK_VERIFY(vfork_cmd("cat", "input", "", 0).get() == "input");
    TERARK_VERIFY(capture(self + " --signal", 0) == "0\n");
    std::string expected = std::to_string(SIGTERM) + "\n";
    TERARK_VERIFY(capture(self + " --signal", SIGTERM) == expected);
    TERARK_VERIFY(vfork_cmd(self + " --signal", "", "", SIGTERM).get() == expected);
    TERARK_VERIFY_EQ(access(marker.c_str(), F_OK), -1); // No Bash startup.
    TERARK_VERIFY(capture("printf 'quoted words'") == "quoted words");
    TERARK_VERIFY_EQ(access(marker.c_str(), F_OK), 0); // Shell syntax uses Bash.

    ProcPipeStream pp;
    TERARK_VERIFY(!pp.xopen("test-non-existed-file", "r", SIGKILL));
    TERARK_VERIFY_EQ(errno, ENOENT);
    TERARK_VERIFY(!pp.xopen("/bin/true", "r", NSIG));
    TERARK_VERIFY_EQ(errno, EINVAL);
    TERARK_VERIFY(pp.xopen("/bin/false", "r", 0));
    int status = pp.xclose();
    TERARK_VERIFY(WIFEXITED(status) && WEXITSTATUS(status) == 1);
    for (int kind : {0, 1}) {
        int calls = 0;
        pp.open("/bin/true", "r", [&](ProcPipeStream*) {
            ++calls;
            if (kind == 0)
                throw std::runtime_error("callback failure");
            throw 1;
        });
        pp.close();
        pp.wait_finish();
        TERARK_VERIFY_EQ(calls, 1);
        TERARK_VERIFY_EQ(pp.err_code(), -1);
    }
    try {
        vfork_cmd("cat", [](ProcPipeStream&) {
            throw std::runtime_error("writer failure");
        }, "", 0).get();
        TERARK_DIE("missing writer exception");
    } catch (const std::runtime_error& ex) {
        TERARK_VERIFY(strcmp(ex.what(), "writer failure") == 0);
    }

    if (had_env)
        setenv("BASH_ENV", saved_env.c_str(), 1);
    else
        unsetenv("BASH_ENV");
    unlink(marker.c_str());
    unlink(env_file.c_str());
    rmdir(directory);
    test_parent_death(self);
}

int main(int argc, char* argv[]) {
    alarm(20);
    char self_path[4096];
    ssize_t len = readlink("/proc/self/exe", self_path, sizeof(self_path));
    TERARK_VERIFY_GT(len, 0);
    std::string self(self_path, len);
    if (argc >= 2 && strcmp(argv[1], "--signal") == 0) {
        int sig = 0;
        TERARK_VERIFY_EQ(prctl(PR_GET_PDEATHSIG, &sig), 0);
        printf("%d\n", sig);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--hold") == 0) {
        pid_t pid = getpid();
        int fd = atoi(argv[2]);
        TERARK_VERIFY_EQ(write(fd, &pid, sizeof(pid)), sizeof(pid));
        close(fd);
        for (;;) pause();
    }
    if (argc == 3 && strcmp(argv[1], "--owner") == 0) {
        ProcPipeStream pp(self + " --hold " + argv[2], "r", SIGKILL);
        pp.close();
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--backend") == 0) {
        setenv("SubProcHowSpawn", argv[2], 1);
    }

	using namespace terark;
    {
        printf("1 begin...\n");
        LineBuf line;
        ProcPipeStream pp("echo aaaa", "r");
        printf("reading result\n");
        //line.getline(pp);

        line.read_all(pp);
        line.chomp();
        assert(line.size() == 4);
        printf("read result = len=%zd : %s\n", line.n, line.p);
        assert(fstring(line) == "aaaa");
        printf("1 passed\n");
    }

    {
        printf("2 begin...\n");
        ProcPipeStream pp("cat > proc.test.tmp", "w");
        fprintf(pp, "%s\n", "bbbb");
        pp.close();

        LineBuf line;
        line.read_all("proc.test.tmp");
        line.chomp();
        assert(fstring(line) == "bbbb");
        printf("2 passed\n");
        ::remove("proc.test.tmp");
    }

    printf("3 begin...\n");
    try {
        ProcPipeStream pp("test-non-existed-file", "r");
        pp.close();
        assert(pp.err_code() != 0);
    }
    catch (const std::exception&) {
    }
    printf("3 passed\n");

    {
        printf("4 begin...\n");
        fflush(stdout);
        std::string res = vfork_cmd("(echo aa; cat)", "bb").get();
    //  printf("res.size() = %zd: %s\n", res.size(), res.c_str());
        assert(res == "aa\nbb");
        printf("4 passed\n");
    }

    test_direct_process(self);

	return 0;
}
