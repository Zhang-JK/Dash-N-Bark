//
// CrashHandler — signal-driven stack trace dumper using cpptrace.
//
// SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL: dump the crashing thread's stack to
// logs/crash_<pid>_<ts>.trace, then re-raise the signal with the default
// handler so the watchdog still observes the canonical exit code.
//
// SIGUSR1 (sent by watchdog when heartbeats go stale): dump *all* threads'
// stacks via per-thread SIGUSR2 to logs/deadlock_<pid>_<ts>.trace, then
// return without exiting. The watchdog then proceeds with SIGTERM/SIGKILL.
//
// Note: cpptrace's symbol resolution uses malloc and is therefore not
// strictly async-signal-safe. We accept that risk: the worst case is the
// handler crashes itself, which is no worse than the original outcome.
//

#include "CrashHandler.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <mutex>
#include <string>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

#include <cpptrace/cpptrace.hpp>

namespace {
    constexpr const char* CRASH_DIR = "logs";

    // SIGUSR2 path: each thread that receives it dumps its own backtrace into
    // this fd (set by the SIGUSR1 dispatcher before it kicks off per-thread
    // signaling). Cleared back to -1 once SIGUSR1 is done.
    std::atomic<int> g_dump_fd{-1};
    // Acks counted by the SIGUSR2 handler so the dispatcher knows when to stop
    // waiting. Atomic increment is async-signal-safe on every platform we care
    // about (lock-free for int on x86_64/arm64).
    std::atomic<int> g_dump_acks{0};

    // Single-writer mutex protecting the shared crash log file fd while
    // multiple threads append their traces. SIGUSR2 handlers across threads
    // serialize on this; the small risk of priority inversion is acceptable
    // because we're already in a diagnostic-only path.
    std::mutex g_dump_mutex;

    pid_t gettid_compat() {
        return static_cast<pid_t>(::syscall(SYS_gettid));
    }

    // Build "logs/<prefix>_<pid>_<unixsecs>.trace" without using std::string
    // formatting in the hot path. snprintf into a stack buffer is signal-safe
    // enough in practice.
    void make_trace_path(const char* prefix, char* out, size_t out_size) {
        std::snprintf(out, out_size, "%s/%s_%d_%ld.trace",
                      CRASH_DIR, prefix, static_cast<int>(::getpid()),
                      static_cast<long>(::time(nullptr)));
    }

    void write_str(int fd, const char* s) {
        if (fd < 0) return;
        ::write(fd, s, std::strlen(s));
    }

    // Render a cpptrace::stacktrace to an open fd. cpptrace's own to_string()
    // already includes function names + file:line where available.
    void dump_trace_to_fd(int fd, const char* heading) {
        std::lock_guard<std::mutex> lg(g_dump_mutex);
        write_str(fd, heading);
        write_str(fd, "\n");
        try {
            auto trace = cpptrace::generate_trace(/*skip=*/2);
            auto s = trace.to_string(/*color=*/false);
            ::write(fd, s.data(), s.size());
            write_str(fd, "\n");
        } catch (...) {
            write_str(fd, "<cpptrace failed to generate trace>\n");
        }
    }

    void crash_handler(int sig, siginfo_t* info, void* /*ucontext*/) {
        std::error_code ec;
        std::filesystem::create_directories(CRASH_DIR, ec);
        char path[256];
        make_trace_path("crash", path, sizeof(path));
        int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) fd = STDERR_FILENO;

        char header[256];
        std::snprintf(header, sizeof(header),
                      "=== CRASH signal=%d (%s) tid=%d addr=%p ===",
                      sig, ::strsignal(sig), static_cast<int>(gettid_compat()),
                      info ? info->si_addr : nullptr);
        dump_trace_to_fd(fd, header);

        if (fd != STDERR_FILENO) ::close(fd);

        // Re-raise with the default handler so the kernel still produces a
        // core dump (if ulimit allows) and the watchdog sees the original
        // exit signal.
        struct sigaction dfl{};
        dfl.sa_handler = SIG_DFL;
        ::sigaction(sig, &dfl, nullptr);
        ::raise(sig);
    }

    void per_thread_dump_handler(int /*sig*/, siginfo_t* /*info*/, void* /*uc*/) {
        int fd = g_dump_fd.load(std::memory_order_acquire);
        if (fd < 0) {
            g_dump_acks.fetch_add(1, std::memory_order_release);
            return;
        }
        char header[128];
        std::snprintf(header, sizeof(header), "--- thread tid=%d ---",
                      static_cast<int>(gettid_compat()));
        dump_trace_to_fd(fd, header);
        g_dump_acks.fetch_add(1, std::memory_order_release);
    }

    // Iterate /proc/self/task to enumerate every thread in the process and
    // SIGUSR2 each one. Returns the count of signals sent (one per other
    // thread; we always count ourselves separately).
    int signal_all_threads_except_self(pid_t self_tid) {
        DIR* d = ::opendir("/proc/self/task");
        if (!d) return 0;
        int sent = 0;
        struct dirent* ent;
        while ((ent = ::readdir(d)) != nullptr) {
            if (ent->d_name[0] < '0' || ent->d_name[0] > '9') continue;
            pid_t tid = static_cast<pid_t>(std::atoi(ent->d_name));
            if (tid == 0 || tid == self_tid) continue;
            if (::syscall(SYS_tgkill, ::getpid(), tid, SIGUSR2) == 0) {
                ++sent;
            }
        }
        ::closedir(d);
        return sent;
    }

    void deadlock_dump_handler(int /*sig*/, siginfo_t* /*info*/, void* /*uc*/) {
        std::error_code ec;
        std::filesystem::create_directories(CRASH_DIR, ec);
        char path[256];
        make_trace_path("deadlock", path, sizeof(path));
        int fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) return;

        char header[160];
        std::snprintf(header, sizeof(header),
                      "=== DEADLOCK DUMP requested via SIGUSR1, pid=%d, ts=%ld ===",
                      static_cast<int>(::getpid()), static_cast<long>(::time(nullptr)));
        write_str(fd, header);
        write_str(fd, "\n");

        pid_t self_tid = gettid_compat();

        // Self first so the dispatcher's stack is in the file even if the
        // per-thread broadcast fails.
        char self_header[128];
        std::snprintf(self_header, sizeof(self_header),
                      "--- thread tid=%d (dispatcher) ---", static_cast<int>(self_tid));
        dump_trace_to_fd(fd, self_header);

        // Publish fd to peers and broadcast SIGUSR2.
        g_dump_acks.store(0, std::memory_order_release);
        g_dump_fd.store(fd, std::memory_order_release);
        int sent = signal_all_threads_except_self(self_tid);

        // Wait up to 2s for all peers to ack. We don't sleep precisely; busy-
        // spin with usleep nibbles so we exit as soon as everyone reports in.
        for (int i = 0; i < 200 && g_dump_acks.load(std::memory_order_acquire) < sent; ++i) {
            ::usleep(10000); // 10ms
        }

        if (g_dump_acks.load(std::memory_order_acquire) < sent) {
            char tail[128];
            std::snprintf(tail, sizeof(tail),
                          "[only %d/%d peer threads responded before timeout]\n",
                          g_dump_acks.load(std::memory_order_acquire), sent);
            write_str(fd, tail);
        }

        g_dump_fd.store(-1, std::memory_order_release);
        ::close(fd);
    }

    void install_sigaction(int sig, void (*handler)(int, siginfo_t*, void*),
                            int extra_flags = 0) {
        struct sigaction sa{};
        sa.sa_sigaction = handler;
        sa.sa_flags = SA_SIGINFO | extra_flags;
        ::sigemptyset(&sa.sa_mask);
        ::sigaction(sig, &sa, nullptr);
    }
}

namespace CrashHandler {
    void install() {
        // Warm cpptrace before any signal fires so the first trace doesn't
        // pay the lazy-load cost while we're already in a crash.
        try {
            (void)cpptrace::generate_trace(0, 1).to_string();
        } catch (...) {}

        // Async-signal: switch to a separate stack so the handler can still
        // run when SIGSEGV was caused by stack overflow. Size picked to be
        // comfortably above MINSIGSTKSZ on every glibc version (it's a
        // sysconf() call on glibc 2.34+, not a constant, so we hard-code).
        static constexpr size_t ALT_STACK_SIZE = 64 * 1024;
        static thread_local char alt_stack_storage[ALT_STACK_SIZE];
        stack_t ss{};
        ss.ss_sp = alt_stack_storage;
        ss.ss_size = ALT_STACK_SIZE;
        ss.ss_flags = 0;
        ::sigaltstack(&ss, nullptr);

        install_sigaction(SIGSEGV, crash_handler, SA_ONSTACK | SA_RESETHAND);
        install_sigaction(SIGABRT, crash_handler, SA_ONSTACK | SA_RESETHAND);
        install_sigaction(SIGBUS,  crash_handler, SA_ONSTACK | SA_RESETHAND);
        install_sigaction(SIGFPE,  crash_handler, SA_ONSTACK | SA_RESETHAND);
        install_sigaction(SIGILL,  crash_handler, SA_ONSTACK | SA_RESETHAND);

        // SIGUSR1: dispatcher (any thread can take it; we let the kernel pick).
        install_sigaction(SIGUSR1, deadlock_dump_handler);
        // SIGUSR2: per-thread dump. Don't mask it so the dispatcher's tgkill
        // wakes targeted threads even if they're holding other signals.
        install_sigaction(SIGUSR2, per_thread_dump_handler);

        // Also catch uncaught C++ exceptions (which would otherwise call
        // std::terminate and lose the original throw site).
        cpptrace::register_terminate_handler();
    }
}
