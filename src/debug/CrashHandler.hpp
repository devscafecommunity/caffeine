#pragma once

namespace Caffeine::Debug {

/// Install SIGSEGV/SIGABRT/SIGILL/SIGFPE handlers that dump a backtrace
/// to stderr and ~/.config/caffeine/crash.log.
void installCrashHandler();

/// Record a short breadcrumb for the next crash dump (async-signal-safe if
/// `text` is a string literal).
void setCrashBreadcrumb(const char* text);

}  // namespace Caffeine::Debug
