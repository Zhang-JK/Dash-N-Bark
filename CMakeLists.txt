cmake_minimum_required(VERSION 3.25)

# global settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")
add_compile_options($<$<CONFIG:Debug>:-ggdb>)

project(Dash-N-Bark)

# dependencies
find_package(TBB REQUIRED COMPONENTS tbb)

# compile libs
add_subdirectory(libs/DPP)
add_subdirectory(libs/stdexec)
add_subdirectory(libs/spdlog)
add_subdirectory(libs/soci)
add_subdirectory(libs/cpr)
add_subdirectory(libs/json)
# Bungee uses ${CMAKE_INSTALL_PREFIX} literally in its install() destinations, so the
# path is expanded at configure time rather than at install time. Override the default
# (/usr/local) to the local build directory so bungee never tries to write to a system
# path. The guard ensures a user-supplied -DCMAKE_INSTALL_PREFIX is still respected.
# All other libraries use relative DESTINATION paths, so cmake --install --prefix
# overrides their location at install time regardless of this configure-time value.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}" CACHE PATH "Install path prefix" FORCE)
endif()
# BUNGEE_PRESET is appended to CMAKE_INSTALL_PREFIX in bungee's install() calls;
# setting it to "bin" routes bungee's library and executable to build/bin.
set(BUNGEE_PRESET "bin" CACHE STRING "Bungee install location suffix (relative to CMAKE_INSTALL_PREFIX)" FORCE)
add_subdirectory(libs/bungee)
if (NOT DEFINED OPUS_INCLUDE_DIRS OR OPUS_INCLUDE_DIRS STREQUAL "" OR OPUS_INCLUDE_DIRS MATCHES "-NOTFOUND")
    message(FATAL_ERROR "OPUS is needed for voice features: add the opus submodule or call\
    find_package(Opus REQUIRED). OPUS include dirs or libraries not found.")
endif()
message("Found OPUS: ${OPUS_LIBRARIES} in ${OPUS_INCLUDE_DIRS}")

# cpptrace: in-process stack traces for crash + deadlock diagnostics.
# Pulled in via FetchContent so the build stays self-contained. cpptrace
# auto-detects an available DWARF backend (libdw on this host).
include(FetchContent)
FetchContent_Declare(
    cpptrace
    GIT_REPOSITORY https://github.com/jeremy-rifkin/cpptrace.git
    GIT_TAG        v0.7.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(cpptrace)

# compile local libs
add_subdirectory(Audio-Mixer)
add_subdirectory(Stream-Fetch)

# compile main app
add_executable(${PROJECT_NAME}
        main.cpp
        BotRouter.cpp
        ToolInterface.cpp
        CrashHandler.cpp
        Commands/SoundpadCommand.h
        Commands/AddCommand.h
        Commands/ParrotCommand.h
        Commands/SearchCommand.h
        Commands/JoinEffectCommand.h
)
target_link_libraries(${PROJECT_NAME} PRIVATE dpp TBB::tbb stdexec audio_mixer spdlog stream_fetch nlohmann_json cpptrace::cpptrace)
# stdexec's __sexpr lives in an anonymous namespace by design, so any coroutine
# that builds a sender pipeline ends up with a synthesised Frame struct whose
# fields have internal linkage. Our Commands/*.h coroutines only ever expand in
# one TU each, so the ODR risk -Wsubobject-linkage flags doesn't apply.
target_compile_options(${PROJECT_NAME} PRIVATE -fcoroutines -std=c++20 -Wno-subobject-linkage)
# Export symbols into the dynamic table so cpptrace can resolve frame addresses
# back to function names even in release builds where DWARF info is stripped.
target_link_options(${PROJECT_NAME} PRIVATE -rdynamic)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(NOT EXISTS ${CMAKE_BINARY_DIR}/bin/yt-dlp_linux)
    message(STATUS "Downloading yt-dlp_linux...")
    file(DOWNLOAD "https://github.com/yt-dlp/yt-dlp/releases/download/2026.02.04/yt-dlp_linux"
            ${CMAKE_BINARY_DIR}/bin/yt-dlp_linux
            SHOW_PROGRESS
            STATUS download_status
    )
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        install(PROGRAMS ${CMAKE_BINARY_DIR}/bin/yt-dlp_linux DESTINATION bin)
    endif()
    list(GET download_status 0 status_code)
    list(GET download_status 1 status_string)
    if(NOT status_code EQUAL 0)
        message(WARNING "Download failed with status: ${status_code} - ${status_string}")
    endif()
endif()
file(CHMOD ${CMAKE_BINARY_DIR}/bin/yt-dlp_linux PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

install(TARGETS ${PROJECT_NAME} dpp stdexec audio_mixer spdlog stream_fetch nlohmann_json bungee_library
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)

# Tests (opt-in via -DDASH_N_BARK_BUILD_TESTS=ON, default ON in Debug).
option(DASH_N_BARK_BUILD_TESTS "Build test harnesses" ON)
if(DASH_N_BARK_BUILD_TESTS)
    add_subdirectory(tests/crash_handler)
endif()