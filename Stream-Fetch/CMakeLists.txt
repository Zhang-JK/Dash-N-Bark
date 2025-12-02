cmake_minimum_required(VERSION 3.25)

project(Stream-Fetch)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Gather all source and header files under this subdirectory
file(GLOB_RECURSE PROJECT_LOCAL_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
)
file(GLOB_RECURSE PROJECT_LOCAL_HEADERS CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
)

if(NOT PROJECT_LOCAL_SOURCES)
    message(WARNING "${PROJECT_NAME}: No source files found in ${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

add_library(${PROJECT_NAME}
        ${PROJECT_LOCAL_SOURCES}
        ${PROJECT_LOCAL_HEADERS}
)

target_include_directories(${PROJECT_NAME}
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# Use modern C++ settings (inherit from parent, but ensure at least C++20)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
# propagate useful compile options if needed
target_compile_options(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:-g> -std=c++20)
# Link dependencies
target_link_libraries(${PROJECT_NAME} spdlog cpr::cpr nlohmann_json)

# Provide a namespaced alias so external CMake files can easily link to it
add_library(stream_fetch ALIAS ${PROJECT_NAME})
add_library(StreamFetch::stream_fetch ALIAS ${PROJECT_NAME})
