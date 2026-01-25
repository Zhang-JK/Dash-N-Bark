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
if (NOT DEFINED OPUS_INCLUDE_DIRS OR OPUS_INCLUDE_DIRS STREQUAL "" OR OPUS_INCLUDE_DIRS MATCHES "-NOTFOUND")
    message(FATAL_ERROR "OPUS is needed for voice features: add the opus submodule or call\
    find_package(Opus REQUIRED). OPUS include dirs or libraries not found.")
endif()
message("Found OPUS: ${OPUS_LIBRARIES} in ${OPUS_INCLUDE_DIRS}")

# compile local libs
add_subdirectory(Audio-Mixer)
add_subdirectory(Stream-Fetch)

# compile main app
add_executable(${PROJECT_NAME}
        main.cpp
        BotRouter.cpp
        ToolInterface.cpp
        Commands/CommandBase.h
        Commands/JoinCommand.h
        Commands/LeaveCommand.h
        Commands/StreamCommand.h
)
target_link_libraries(${PROJECT_NAME} PRIVATE dpp TBB::tbb stdexec audio_mixer spdlog stream_fetch nlohmann_json)
target_compile_options(${PROJECT_NAME} PRIVATE -fcoroutines -std=c++20)

install(TARGETS ${PROJECT_NAME} dpp stdexec audio_mixer spdlog stream_fetch nlohmann_json
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)