cmake_minimum_required(VERSION 3.25)

project(Audio-Mixer)

# Gather all source and header files under this subdirectory
file(GLOB_RECURSE AUDIO_MIXER_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c
  ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc
)
file(GLOB_RECURSE AUDIO_MIXER_HEADERS CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
)

# Create library (choose STATIC by default; allow building shared if global BUILD_SHARED_LIBS is set)
if(NOT AUDIO_MIXER_SOURCES)
  message(WARNING "Audio-Mixer: No source files found in ${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

add_library(audio_mixer
  ${AUDIO_MIXER_SOURCES}
  ${AUDIO_MIXER_HEADERS} # headers can be listed for IDE visibility (optional)
)

# Expose include directory to consumers
target_include_directories(audio_mixer
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Use modern C++ settings (inherit from parent, but ensure at least C++20)
target_compile_features(audio_mixer PUBLIC cxx_std_20)
# propagate useful compile options if needed
target_compile_options(audio_mixer PRIVATE $<$<CONFIG:Debug>:-g>)

# Link OPUS if variables are available (top-level CMake verifies OPUS)
if(DEFINED OPUS_LIBRARIES AND NOT "${OPUS_LIBRARIES}" STREQUAL "")
  target_link_libraries(audio_mixer PUBLIC ${OPUS_LIBRARIES})
endif()

# Provide a namespaced alias so external CMake files can easily link to it
add_library(AudioMixer::audio_mixer ALIAS audio_mixer)

# Optional: helpful messages
message(STATUS "Audio-Mixer: ${AUDIO_MIXER_SOURCES} sources, headers in include/")
