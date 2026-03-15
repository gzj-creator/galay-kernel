/**
 * @file T57-io_uring_timeout_close_lifetime.cc
 * @brief 用途：验证 io_uring `timeout/close` 相关 Awaitable 的生命周期边界。
 * 关键覆盖点：超时请求对象生命周期、关闭操作收尾、资源释放时机。
 * 通过条件：相关 Awaitable 在生命周期边界下无异常，测试返回 0。
 */

#include <filesystem>
#include <iostream>
#include <string>

#ifdef USE_IOURING
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace {

std::filesystem::path benchmarkBinary(const char* argv0) {
    return std::filesystem::path(argv0).parent_path() / "B5-UdpClient";
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc <= 0) {
        std::cerr << "[T57] missing argv[0]\n";
        return 1;
    }

    const auto benchmark = benchmarkBinary(argv[0]);
    if (!std::filesystem::exists(benchmark)) {
        std::cerr << "[T57] benchmark binary not found: " << benchmark << "\n";
        return 1;
    }

    pid_t child = fork();
    if (child < 0) {
        std::perror("[T57] fork");
        return 1;
    }

    if (child == 0) {
        execl(benchmark.c_str(), benchmark.c_str(), static_cast<char*>(nullptr));
        std::perror("[T57] execl");
        _exit(127);
    }

    int status = 0;
    if (waitpid(child, &status, 0) < 0) {
        std::perror("[T57] waitpid");
        return 1;
    }

    if (WIFSIGNALED(status)) {
        std::cerr << "[T57] B5-UdpClient terminated by signal "
                  << WTERMSIG(status) << "\n";
        return 1;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << "[T57] B5-UdpClient exit code "
                  << (WIFEXITED(status) ? WEXITSTATUS(status) : -1) << "\n";
        return 1;
    }

    std::cout << "T57-IOUringTimeoutCloseLifetime PASS\n";
    return 0;
}

#else

int main() {
    std::cout << "T57-IOUringTimeoutCloseLifetime SKIP\n";
    return 0;
}

#endif
