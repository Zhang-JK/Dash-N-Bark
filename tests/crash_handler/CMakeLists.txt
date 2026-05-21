# Test harness for CrashHandler. Built as a standalone executable so the
# test driver can run each crash scenario in isolation.
add_executable(crash_handler_test
    test_main.cpp
    ${CMAKE_SOURCE_DIR}/CrashHandler.cpp
)
target_include_directories(crash_handler_test PRIVATE ${CMAKE_SOURCE_DIR})
# stdexec is linked so the deadlock/hang scenarios can dispatch parked
# workers the same way the bot does (BotRouter::launchHandlerOnPool).
target_link_libraries(crash_handler_test PRIVATE cpptrace::cpptrace stdexec pthread)
# Same -rdynamic as the real bot so cpptrace can resolve symbols.
target_link_options(crash_handler_test PRIVATE -rdynamic)
target_compile_options(crash_handler_test PRIVATE -std=c++20)
