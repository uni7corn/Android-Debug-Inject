#include <sys/wait.h>
#include <unistd.h>
#include "misc.hpp"

int new_daemon_thread(thread_entry entry, void *arg) {
    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    errno = pthread_create(&thread, &attr, entry, arg);
    if (errno) {
        PLOGE("pthread_create");
    }
    return errno;
}

int parse_int(std::string_view s) {
    int val = 0;
    for (char c : s) {
        if (!c) break;
        if (c > '9' || c < '0')
            return -1;
        val = val * 10 + c - '0';
    }
    return val;
}

std::list<std::string> split_str(std::string_view s, std::string_view delimiter) {
    std::list<std::string> ret;
    size_t pos = 0;
    while (pos < s.size()) {
        auto next = s.find(delimiter, pos);
        if (next == std::string_view::npos) {
            ret.emplace_back(s.substr(pos));
            break;
        }
        ret.emplace_back(s.substr(pos, next - pos));
        pos = next + delimiter.size();
    }
    return ret;
}

std::string join_str(const std::list<std::string>& list, std::string_view delimiter) {
    std::string ret;
    for (auto& s : list) {
        if (!ret.empty())
            ret += delimiter;
        ret += s;
    }
    return ret;
}


int fork_dont_care() {
    if (int pid = fork()) {
        waitpid(pid, nullptr, 0);
        return pid;
    } else if (fork()) {
        exit(0);
    }
    return 0;
}

int vssprintf(char *dest, size_t size, const char *fmt, va_list ap) {
    if (size > 0) {
        *dest = 0;
        return std::min(vsnprintf(dest, size, fmt, ap), (int) size - 1);
    }
    return -1;
}

int ssprintf(char *dest, size_t size, const char *fmt, ...) {
    va_list va;
    va_start(va, fmt);
    int r = vssprintf(dest, size, fmt, va);
    va_end(va);
    return r;
}