#include "debug/CrashHandler.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <sys/stat.h>

#if defined(__linux__) || defined(__APPLE__)
#include <execinfo.h>
#endif

namespace Caffeine::Debug {
namespace {

constexpr int kMaxBreadcrumbs = 8;
constexpr int kMaxFrames = 64;

const char* g_breadcrumbs[kMaxBreadcrumbs] = {};
int g_breadcrumbCount = 0;
volatile sig_atomic_t g_handling = 0;

const char* signalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (segmentation fault)";
        case SIGABRT: return "SIGABRT";
        case SIGILL: return "SIGILL";
        case SIGFPE: return "SIGFPE";
        case SIGBUS: return "SIGBUS";
        default: return "signal";
    }
}

void writeFd(int fd, const char* text) {
    if (!text) return;
    const size_t n = std::strlen(text);
    if (n > 0) {
        (void)!write(fd, text, n);
    }
}

FILE* openCrashLog() {
    const char* home = std::getenv("HOME");
    char path[512];
    if (home && home[0] != '\0') {
        char dir[512];
        std::snprintf(dir, sizeof(dir), "%s/.config/caffeine", home);
        mkdir(dir, 0755);
        std::snprintf(path, sizeof(path), "%s/.config/caffeine/crash.log", home);
    } else {
        std::snprintf(path, sizeof(path), "doppio-crash.log");
    }
    return std::fopen(path, "a");
}

void dumpCrash(int sig) {
    char header[256];
    const std::time_t now = std::time(nullptr);
    char timeBuf[64] = "unknown";
    if (std::tm* tm = std::localtime(&now)) {
        std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tm);
    }
    std::snprintf(header, sizeof(header),
                  "\n===== Doppio crash %s pid=%d %s =====\n",
                  signalName(sig), static_cast<int>(getpid()), timeBuf);

    writeFd(STDERR_FILENO, header);

    FILE* log = openCrashLog();
    if (log) {
        std::fputs(header, log);
        std::fputs("Breadcrumbs (most recent last):\n", log);
        const int count = g_breadcrumbCount < kMaxBreadcrumbs ? g_breadcrumbCount : kMaxBreadcrumbs;
        if (count == 0) {
            std::fputs("  (none)\n", log);
        }
        for (int i = 0; i < count; ++i) {
            const char* crumb = g_breadcrumbs[i];
            std::fprintf(log, "  %s\n", crumb ? crumb : "(null)");
        }
    }
    writeFd(STDERR_FILENO, "Breadcrumbs (most recent last):\n");
    const int count = g_breadcrumbCount < kMaxBreadcrumbs ? g_breadcrumbCount : kMaxBreadcrumbs;
    for (int i = 0; i < count; ++i) {
        writeFd(STDERR_FILENO, "  ");
        writeFd(STDERR_FILENO, g_breadcrumbs[i] ? g_breadcrumbs[i] : "(null)");
        writeFd(STDERR_FILENO, "\n");
    }

#if defined(__linux__) || defined(__APPLE__)
    void* frames[kMaxFrames];
    const int n = backtrace(frames, kMaxFrames);
    writeFd(STDERR_FILENO, "Backtrace:\n");
    if (log) std::fputs("Backtrace:\n", log);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    if (log) {
        char** symbols = backtrace_symbols(frames, n);
        if (symbols) {
            for (int i = 0; i < n; ++i) {
                std::fprintf(log, "  %s\n", symbols[i]);
            }
            std::free(symbols);
        }
        std::fputs("===== end crash =====\n", log);
        std::fflush(log);
        std::fclose(log);
    }
#else
    if (log) {
        std::fputs("Backtrace unavailable on this platform.\n===== end crash =====\n", log);
        std::fclose(log);
    }
#endif

    writeFd(STDERR_FILENO, "Crash log: ~/.config/caffeine/crash.log\n");
}

extern "C" void caffeineCrashSignal(int sig) {
    if (g_handling) {
        _exit(128 + sig);
    }
    g_handling = 1;
    dumpCrash(sig);
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

}  // namespace

void installCrashHandler() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = caffeineCrashSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
#ifdef SIGBUS
    sigaction(SIGBUS, &sa, nullptr);
#endif
}

void setCrashBreadcrumb(const char* text) {
    if (!text) return;
    if (g_breadcrumbCount < kMaxBreadcrumbs) {
        g_breadcrumbs[g_breadcrumbCount++] = text;
        return;
    }
    for (int i = 1; i < kMaxBreadcrumbs; ++i) {
        g_breadcrumbs[i - 1] = g_breadcrumbs[i];
    }
    g_breadcrumbs[kMaxBreadcrumbs - 1] = text;
}

}  // namespace Caffeine::Debug
