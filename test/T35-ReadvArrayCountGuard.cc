#include <array>
#include <cstdlib>
#include <iostream>

#include <csignal>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include "galay-kernel/async/TcpSocket.h"

using namespace galay::async;

namespace {

bool childAbortedOnInvalidReadvCount() {
    pid_t pid = fork();
    if (pid == 0) {
        TcpSocket socket;
        std::array<struct iovec, 2> iovecs{};
        auto awaitable = socket.readv(iovecs, 3);
        (void)awaitable;
        _exit(0);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return false;
    }
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

bool childAbortedOnInvalidWritevCount() {
    pid_t pid = fork();
    if (pid == 0) {
        TcpSocket socket;
        char header[] = "guard";
        char body[] = "check";
        std::array<struct iovec, 2> iovecs{};
        iovecs[0].iov_base = header;
        iovecs[0].iov_len = sizeof(header) - 1;
        iovecs[1].iov_base = body;
        iovecs[1].iov_len = sizeof(body) - 1;
        auto awaitable = socket.writev(iovecs, 3);
        (void)awaitable;
        _exit(0);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return false;
    }
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

}  // namespace

int main() {
    const bool readvGuarded = childAbortedOnInvalidReadvCount();
    const bool writevGuarded = childAbortedOnInvalidWritevCount();

    if (!readvGuarded || !writevGuarded) {
        std::cerr << "T35 failed:"
                  << " readv_guarded=" << readvGuarded
                  << " writev_guarded=" << writevGuarded
                  << std::endl;
        return 1;
    }

    std::cout << "T35 passed" << std::endl;
    return 0;
}
