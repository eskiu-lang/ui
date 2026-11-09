// The driver channel of the native hosts (docs/agents.md), in a dev build only (UI_TEST):
// a socket the app listens on (TCP on 127.0.0.1 on the iOS simulator and macOS, an abstract Unix
// socket on Android that `adb forward` reaches) taking one JSON request per line. Each
// request runs on the UI thread: the core answers it (ui_driver), or asks the host to do its
// part first ({"host": ...}), which the host's driverCarry does before ui_driver_done answers.
// The app's standard output and error are kept too, for `logs`, and still printed.
//
// The host defines, before including this file:
//   static void driverOnMain(std::function<void()> work);   // runs `work` on the UI thread, now or soon
//   static std::string driverCarry(const std::string &directive);  // the host's part, its result
// and, with UI_DRIVER_HOST_REQUESTS defined, the requests it answers without the core:
//   static bool driverHostRequest(const std::string &request, std::string &answer);
#pragma once
#ifdef UI_TEST
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdio>
#include <ctime>
#include <sys/time.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

// The member `key` of a directive: a number, a flag, a string (unescaped).
static double driverNumber(const std::string &d, const char *key) {
    std::string pattern = std::string("\"") + key + "\": ";
    size_t at = d.find(pattern);
    return at == std::string::npos ? 0 : std::strtod(d.c_str() + at + pattern.size(), nullptr);
}
static bool driverFlag(const std::string &d, const char *key) {
    return d.find(std::string("\"") + key + "\": true") != std::string::npos;
}
static std::string driverText(const std::string &d, const char *key) {
    std::string pattern = std::string("\"") + key + "\": \"";
    size_t at = d.find(pattern);
    std::string out;
    if (at == std::string::npos) return out;
    for (size_t i = at + pattern.size(); i < d.size() && d[i] != '"'; i++) {
        char c = d[i];
        if (c != '\\' || i + 1 >= d.size()) { out += c; continue; }
        char e = d[++i];
        if (e == 'n') out += '\n';
        else if (e == 't') out += '\t';
        else if (e == 'r') out += '\r';
        else if (e == 'u' && i + 4 < d.size()) { out += (char)std::strtol(d.substr(i + 1, 4).c_str(), nullptr, 16); i += 4; }
        else out += e;
    }
    return out;
}
static std::string driverOp(const std::string &d) { return driverText(d, "host"); }

// The app's output, the last 500 lines.
static std::mutex driverLogLock;
static std::deque<std::string> driverLogLines;
static std::string driverLogs() {
    std::lock_guard<std::mutex> hold(driverLogLock);
    std::string out;
    for (auto &line : driverLogLines) { out += line; out += '\n'; }
    return out;
}
// Standard output and error into a pipe read by a thread, which keeps each line and writes
// it on to `echo` (the original stderr, or a log).
static void driverCapture(std::function<void(const char *, size_t)> echo) {
    int fds[2];
    if (pipe(fds) != 0) return;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    dup2(fds[1], 1);
    dup2(fds[1], 2);
    std::thread([fd = fds[0], echo]() {
        char buffer[4096];
        std::string line;
        for (;;) {
            ssize_t n = read(fd, buffer, sizeof buffer);
            if (n <= 0) return;
            echo(buffer, (size_t)n);
            std::lock_guard<std::mutex> hold(driverLogLock);
            for (ssize_t i = 0; i < n; i++) {
                if (buffer[i] != '\n') { line += buffer[i]; continue; }
                driverLogLines.push_back(line);
                line.clear();
                if (driverLogLines.size() > 500) driverLogLines.pop_front();
            }
        }
    }).detach();
}

// When this process started, in wall clock ms (0 when unknown), for the cold start `perf`
// reports: the kernel's start time of the process.
static double driverStartedMs() {
#ifdef __APPLE__
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, (int)getpid()};
    struct kinfo_proc info = {};
    size_t size = sizeof info;
    if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0) return 0;
    struct timeval start = info.kp_proc.p_starttime;
    return (double)start.tv_sec * 1000.0 + (double)start.tv_usec / 1000.0;
#elif defined(__linux__)
    // /proc/self/stat field 22: clock ticks after boot, which CLOCK_BOOTTIME counts too.
    FILE *file = std::fopen("/proc/self/stat", "r");
    if (!file) return 0;
    char text[1024] = {};
    size_t n = std::fread(text, 1, sizeof text - 1, file);
    std::fclose(file);
    text[n] = 0;
    const char *at = std::strrchr(text, ')');
    if (!at) return 0;
    at += 2;
    for (int field = 3; field < 22 && *at; field++) {
        at = std::strchr(at, ' ');
        if (!at) return 0;
        at++;
    }
    double ticks = std::strtod(at, nullptr);
    long hz = sysconf(_SC_CLK_TCK);
    struct timespec boot = {}, wall = {};
    clock_gettime(CLOCK_BOOTTIME, &boot);
    clock_gettime(CLOCK_REALTIME, &wall);
    double boot_ms = (double)boot.tv_sec * 1000.0 + (double)boot.tv_nsec / 1e6;
    double wall_ms = (double)wall.tv_sec * 1000.0 + (double)wall.tv_nsec / 1e6;
    return wall_ms - (boot_ms - ticks * 1000.0 / (double)(hz > 0 ? hz : 100));
#else
    return 0;
#endif
}

// The `perf` directive's result: the memory in bytes, then when the process started.
static std::string driverPerf(double memory) {
    char out[96];
    std::snprintf(out, sizeof out, "%.0f %.3f", memory, driverStartedMs());
    return out;
}

// One request, on the UI thread: the core's answer.
static std::string driverHandle(const std::string &request) {
#ifdef UI_DRIVER_HOST_REQUESTS
    // A request the host answers itself, without the core (macOS's screenshot).
    std::string own;
    if (driverHostRequest(request, own)) return own;
#endif
    std::string answer = ui_driver(request.c_str());
    if (answer.rfind("{\"host\": ", 0) != 0) return answer;
    std::string result;
    try { result = driverCarry(answer); } catch (...) { result = "error: the host failed"; }
    return ui_driver_done(result.c_str());
}

// Each connection: its lines, each answered on the UI thread, one line back.
static void driverServe(int client) {
    std::string pending;
    char buffer[4096];
    for (;;) {
        ssize_t n = recv(client, buffer, sizeof buffer, 0);
        if (n <= 0) break;
        pending.append(buffer, (size_t)n);
        size_t end;
        while ((end = pending.find('\n')) != std::string::npos) {
            std::string request = pending.substr(0, end);
            pending.erase(0, end + 1);
            std::mutex lock;
            std::condition_variable done;
            bool finished = false;
            std::string answer;
            driverOnMain([&]() {
                answer = driverHandle(request);
                std::lock_guard<std::mutex> hold(lock);
                finished = true;
                done.notify_one();
            });
            std::unique_lock<std::mutex> wait(lock);
            done.wait(wait, [&]() { return finished; });
            answer += '\n';
            for (size_t sent = 0; sent < answer.size();) {
                ssize_t k = send(client, answer.data() + sent, answer.size() - sent, 0);
                if (k <= 0) break;
                sent += (size_t)k;
            }
        }
    }
    close(client);
}

// Accepts connections on `listener`, one thread each.
static void driverAccept(int listener) {
    std::thread([listener]() {
        for (;;) {
            int client = accept(listener, nullptr, nullptr);
            if (client < 0) continue;
            int on = 1;
#ifdef SO_NOSIGPIPE
            setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
            std::thread(driverServe, client).detach();
        }
    }).detach();
}

// The simulator: TCP on 127.0.0.1:port (UI_AGENT_PORT, 8766 by default).
static bool driverListenTcp(int port) {
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return false;
    int on = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (sockaddr *)&address, sizeof address) != 0 || listen(listener, 4) != 0) { close(listener); return false; }
    driverAccept(listener);
    return true;
}

// Android: the abstract socket `name` (adb forward tcp:N localabstract:NAME), which needs no
// network permission.
static bool driverListenAbstract(const char *name) {
#ifdef __linux__
    int listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) return false;
    sockaddr_un address = {};
    address.sun_family = AF_UNIX;
    size_t n = std::strlen(name);
    std::memcpy(address.sun_path + 1, name, n);
    socklen_t size = (socklen_t)(offsetof(sockaddr_un, sun_path) + 1 + n);
    if (bind(listener, (sockaddr *)&address, size) != 0 || listen(listener, 4) != 0) { close(listener); return false; }
    driverAccept(listener);
    return true;
#else
    (void)name;
    return false;
#endif
}
#endif
