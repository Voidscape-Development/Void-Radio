# Standalone test project for the parts of Void Radio that do not need OBS:
# the tag readers, the now-playing template expander and playlist navigation.
#
#   cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests

cmake_minimum_required(VERSION 3.22)

project(void-radio-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

enable_testing()

set(VOID_RADIO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")

add_library(
  void-radio-testable
  STATIC
  "${VOID_RADIO_SRC}/music/playlist.cpp"
  "${VOID_RADIO_SRC}/music/track-tags.cpp"
  "${VOID_RADIO_SRC}/util/text-template.cpp"
  "${VOID_RADIO_SRC}/util/vr-util.cpp"
  stubs/platform-stubs.cpp
)

target_include_directories(void-radio-testable PUBLIC "${VOID_RADIO_SRC}" "${CMAKE_CURRENT_SOURCE_DIR}/stubs")

foreach(test_name IN ITEMS tag playlist template)
  add_executable(${test_name}-tests "${test_name}-tests.cpp")
  target_link_libraries(${test_name}-tests PRIVATE void-radio-testable)
  add_test(NAME ${test_name} COMMAND ${test_name}-tests)
endforeach()
