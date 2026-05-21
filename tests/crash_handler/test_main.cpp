//
// Test harness for CrashHandler.
//
// Usage: crash_handler_test <scenario>
//   segv       -- dereference null, expects logs/crash_<pid>_<ts>.trace
//   abort      -- call abort(), same output bucket
//   terminate  -- throw uncaught exception, same output bucket
//   deadlock   -- launch parked workers on a stdexec pool, raise SIGUSR1 on
//                 self, exit via std::exit(0) after the dump completes —
//                 expects logs/deadlock_<pid>_<ts>.trace
//   hang       -- launch parked workers on a stdexec pool, block forever;
//                 driver SIGUSR1s us, then SIGTERMs us to verify the
//                 watchdog escalation path
//
// Threaded scenarios use exec::static_thread_pool + stdexec::schedule | then,
// the same pattern the bot uses (see BotRouter::launchHandlerOnPool). The
// pool is intentionally leaked: its destructor would try to join workers
// that are deliberately stuck in parked_worker, which is the deadlock
// condition we're simulating. We exit via std::exit(0) so the leak is
// reaped by the OS.
//

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include <exec/start_detached.hpp>
#include <exec/static_thread_pool.hpp>
#include <stdexec/execution.hpp>

#include "../../CrashHandler.h"

namespace {
    // Park worker threads in a recognizable function so the deadlock trace
    // has a function name we can grep for. One thread wins the mutex; the
    // rest block on it. All of them have parked_worker on the stack — which
    // is exactly the kind of frame a real deadlock dump should expose.
    std::mutex g_park_mutex;

    void parked_worker(int idx) {
        std::lock_guard<std::mutex> lg(g_park_mutex);
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            (void)idx;
        }
    }

    // Launch `n` parked_worker tasks via stdexec, matching the bot's
    // BotRouter::launchHandlerOnPool dispatch pattern. The pool is leaked
    // deliberately — see file header.
    void spawn_parked_workers(int n) {
        auto* pool = new exec::static_thread_pool(static_cast<std::uint32_t>(n));
        auto sched = pool->get_scheduler();
        for (int i = 0; i < n; ++i) {
            exec::start_detached(
                stdexec::schedule(sched)
                | stdexec::then([i]() noexcept { parked_worker(i); })
            );
        }
        // Give the pool a moment to actually pull each task off the queue
        // and enter parked_worker so the trace dump catches them in the
        // expected frame.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    [[noreturn]] void scenario_segv() {
        // Force a null deref through a volatile so the optimizer doesn't
        // helpfully turn it into a __builtin_trap that confuses the test.
        volatile int* p = nullptr;
        *p = 42;
        std::abort(); // unreachable, but keeps [[noreturn]] honest
    }

    [[noreturn]] void scenario_abort() {
        std::abort();
    }

    [[noreturn]] void scenario_terminate() {
        throw std::runtime_error("intentional uncaught test exception");
    }

    [[noreturn]] void scenario_deadlock() {
        spawn_parked_workers(3);
        // Trigger the same code path the watchdog would: send ourselves
        // SIGUSR1. The handler should write logs/deadlock_*.trace including
        // a frame in parked_worker for each pool worker.
        ::raise(SIGUSR1);
        // Give the dispatcher time to ack every peer thread.
        std::this_thread::sleep_for(std::chrono::seconds(3));
        // Bypass destructors — the static_thread_pool would otherwise try
        // to join workers permanently stuck inside parked_worker.
        std::exit(0);
    }

    [[noreturn]] void scenario_hang() {
        spawn_parked_workers(3);
        // Install a SIGTERM handler so the driver can verify clean exit
        // after the dump. exit(0) is async-signal-unsafe in theory but
        // adequate for this test harness.
        std::signal(SIGTERM, [](int) { std::exit(0); });
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <segv|abort|terminate|deadlock|hang>\n", argv[0]);
        return 2;
    }
    CrashHandler::install();
    const std::string s = argv[1];
    if (s == "segv") scenario_segv();
    if (s == "abort") scenario_abort();
    if (s == "terminate") scenario_terminate();
    if (s == "deadlock") scenario_deadlock();
    if (s == "hang") scenario_hang();

    std::fprintf(stderr, "unknown scenario: %s\n", argv[1]);
    return 2;
}
