//
// CrashHandler — install signal handlers that dump stack traces via cpptrace.
//
// Call CrashHandler::install() once at startup, before any thread is spawned.
//

#ifndef DASH_N_BARK_CRASHHANDLER_H
#define DASH_N_BARK_CRASHHANDLER_H

namespace CrashHandler {
    // Install signal handlers for SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL
    // (crash trace dumped to logs/crash_<pid>_<ts>.trace, then re-raised),
    // plus SIGUSR1 (deadlock trace dumped to logs/deadlock_<pid>_<ts>.trace
    // and the process keeps running). Safe to call multiple times; later
    // calls overwrite the previous handler installations.
    void install();
}

#endif //DASH_N_BARK_CRASHHANDLER_H
