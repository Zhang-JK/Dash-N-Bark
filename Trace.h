//
// Trace.h — opt-in instrumentation for stdexec senders and plain scopes.
//
// Disabled by default. To turn on, compile with -DDNB_TRACE_ENABLED. When
// enabled, ScopeTrace and withPoolTrace emit spdlog::trace lines that
// scripts/trace_graph.py turns into a Chrome trace JSON viewable in
// chrome://tracing or https://ui.perfetto.dev.
//
// When disabled, DNB_TRACE_SCOPE expands to a no-op and dnb_trace::withPoolTrace
// is not declared — guard stdexec call sites with `#ifdef DNB_TRACE_ENABLED`
// to match.
//
// tid is reported as a Linux gettid() so traces line up with the per-thread
// stack dumps emitted by CrashHandler on SIGUSR1.
//

#pragma once

#ifdef DNB_TRACE_ENABLED

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

#include <sys/syscall.h>
#include <unistd.h>

#include <spdlog/spdlog.h>
#include <stdexec/execution.hpp>

namespace dnb_trace {

inline long tid() noexcept {
    return static_cast<long>(::syscall(SYS_gettid));
}

// RAII trace for synchronous scopes. Logs enter on ctor and exit + duration
// on dtor. A coroutine that suspends mid-scope can resume on a different
// thread, in which case enter and exit will carry different tids; the python
// trace builder keys off the exit line (which has dur_us) and renders each
// scope on the tid where it completed.
class ScopeTrace {
public:
    explicit ScopeTrace(const char* name) noexcept
        : name_(name), start_(std::chrono::steady_clock::now()) {
        try {
            spdlog::trace("[scope] enter tid={} name={}", tid(), name_);
        } catch (...) {}
    }
    ~ScopeTrace() {
        try {
            const auto dur_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start_).count();
            spdlog::trace("[scope] exit  tid={} name={} dur_us={}",
                          tid(), name_, dur_us);
        } catch (...) {}
    }
    ScopeTrace(const ScopeTrace&) = delete;
    ScopeTrace& operator=(const ScopeTrace&) = delete;

private:
    const char* name_;
    std::chrono::steady_clock::time_point start_;
};

// Wrap a stdexec sender with enter/exit pool-trace logging. The shared
// TraceState guarantees exactly one exit line per launch — its destructor
// logs "dropped" if the pipeline was torn down before completing through
// value/error/stopped (e.g. process shutdown).
template<typename Sender>
auto withPoolTrace(Sender&& s, std::string tag) {
    struct TraceState {
        std::string tag;
        std::chrono::steady_clock::time_point start{};
        std::atomic<bool> exit_logged{false};

        void logExit(const char* channel) noexcept {
            if (exit_logged.exchange(true, std::memory_order_acq_rel)) return;
            long long dur_us = 0;
            if (start.time_since_epoch().count() != 0) {
                dur_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count();
            }
            try {
                spdlog::trace("[pool-trace] exit  tid={} task={} dur_us={} channel={}",
                              tid(), tag, dur_us, channel);
            } catch (...) {}
        }
        ~TraceState() { logExit("dropped"); }
    };
    auto st = std::make_shared<TraceState>();
    st->tag = std::move(tag);
    return stdexec::just()
        | stdexec::then([st]() noexcept {
            st->start = std::chrono::steady_clock::now();
            try {
                spdlog::trace("[pool-trace] enter tid={} task={}",
                              tid(), st->tag);
            } catch (...) {}
        })
        | stdexec::let_value([s = std::forward<Sender>(s)]() mutable {
            return std::move(s);
        })
        | stdexec::then([st](auto&&...) noexcept { st->logExit("value"); })
        | stdexec::upon_error([st](auto&&) noexcept { st->logExit("error"); })
        | stdexec::upon_stopped([st]() noexcept { st->logExit("stopped"); });
}

} // namespace dnb_trace

#define DNB_TRACE_SCOPE_CONCAT_INNER(a, b) a##b
#define DNB_TRACE_SCOPE_CONCAT(a, b) DNB_TRACE_SCOPE_CONCAT_INNER(a, b)
#define DNB_TRACE_SCOPE(name) \
    ::dnb_trace::ScopeTrace DNB_TRACE_SCOPE_CONCAT(_dnb_scope_, __LINE__){name}

#else  // DNB_TRACE_ENABLED

#define DNB_TRACE_SCOPE(name) ((void)0)

#endif // DNB_TRACE_ENABLED
