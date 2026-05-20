//
// Test harness for CrashHandler.
//
// Usage: crash_handler_test <scenario>
//   segv       -- dereference null, expects logs/crash_<pid>_<ts>.trace
//   abort      -- call abort(), same output bucket
//   terminate  -- throw uncaught exception, same output bucket
//   deadlock   -- spawn worker threads, raise SIGUSR1 on self, exit cleanly
//                 after the dump completes — expects logs/deadlock_<pid>_<ts>.trace
//   hang       -- spawn workers, block forever; driver script SIGUSR1s us,
//                 then SIGTERMs us to verify the watchdog escalation path
//

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "../../CrashHandler.h"

namespace {
    // Park worker threads in a recognizable function so the deadlock trace
    // has a function name we can grep for. Each one grabs a mutex they
    // never release, so a real human reading the trace would see exactly
    // where the bot got stuck in a real deadlock.
    std::mutex g_park_mutex;

    void parked_worker(int idx) {
        std::lock_guard<std::mutex> lg(g_park_mutex);
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            (void)idx;
        }
    }

    void spawn_parked_workers(int n) {
        for (int i = 0; i < n; ++i) {
            std::thread(parked_worker, i).detach();
        }
        // Give them a moment to actually be running inside parked_worker so
        // the trace dump catches them in the expected frame.
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

    int scenario_deadlock() {
        spawn_parked_workers(3);
        // Trigger the same code path the watchdog would: send ourselves
        // SIGUSR1. The handler should write logs/deadlock_*.trace including
        // a frame in parked_worker for each peer thread.
        ::raise(SIGUSR1);
        // Give the dispatcher time to ack everyone.
        std::this_thread::sleep_for(std::chrono::seconds(3));
        return 0;
    }

    int scenario_hang() {
        spawn_parked_workers(3);
        // Block on a never-completing condition so the driver can decide
        // when to signal us. We DO install a SIGTERM handler so the driver
        // can verify the bot exits cleanly after the dump.
        std::signal(SIGTERM, [](int) { std::exit(0); });
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(60));
        }
        return 0; // unreachable
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
    if (s == "deadlock") return scenario_deadlock();
    if (s == "hang") return scenario_hang();

    std::fprintf(stderr, "unknown scenario: %s\n", argv[1]);
    return 2;
}
