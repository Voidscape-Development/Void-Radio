# Standalone test project for the parts of Void Radio that do not need OBS:
# the tag readers, the now-playing template expander and playlist navigation.
#
#   cmake -S tests -B build_tests && cmake --build build_tests && ctest --test-dir build_tests

cmake_minimum_required(VERSION 3.22)

project(void-radio-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

enable_testing()

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample)

set(VOID_RADIO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../src")

add_library(
  void-radio-testable
  STATIC
  "${VOID_RADIO_SRC}/audio/audio-decoder.cpp"
  "${VOID_RADIO_SRC}/audio/ffmpeg-util.cpp"
  "${VOID_RADIO_SRC}/audio/ring-buffer.cpp"
  "${VOID_RADIO_SRC}/audio/stream-deck.cpp"
  "${VOID_RADIO_SRC}/music/album-art.cpp"
  "${VOID_RADIO_SRC}/music/gain-envelope.cpp"
  "${VOID_RADIO_SRC}/music/player.cpp"
  "${VOID_RADIO_SRC}/sources/music-widget/spectrum.cpp"
  "${VOID_RADIO_SRC}/sources/music-widget/widget-layout.cpp"
  "${VOID_RADIO_SRC}/sources/music-widget/widget-palette.cpp"
  "${VOID_RADIO_SRC}/ui/editor-snap.cpp"
  "${VOID_RADIO_SRC}/music/playlist.cpp"
  "${VOID_RADIO_SRC}/music/track-tags.cpp"
  "${VOID_RADIO_SRC}/util/text-template.cpp"
  "${VOID_RADIO_SRC}/util/vr-util.cpp"
  stubs/platform-stubs.cpp
)

target_include_directories(void-radio-testable PUBLIC "${VOID_RADIO_SRC}" "${CMAKE_CURRENT_SOURCE_DIR}/stubs")
target_link_libraries(void-radio-testable PUBLIC PkgConfig::FFMPEG Threads::Threads)

add_library(media-fixtures STATIC media-fixtures.cpp)
target_link_libraries(media-fixtures PUBLIC PkgConfig::FFMPEG)

foreach(
  test_name
  IN
  ITEMS tag playlist template player decoder widget-layout editor-snap spectrum palette
)
  add_executable(${test_name}-tests "${test_name}-tests.cpp")
  target_link_libraries(${test_name}-tests PRIVATE void-radio-testable media-fixtures)
  add_test(NAME ${test_name} COMMAND ${test_name}-tests)
endforeach()

# The layout editor's canvas, driven with simulated input on Qt's offscreen
# platform. Built when Qt 6 is available.
find_package(Qt6 QUIET COMPONENTS Widgets)

if(TARGET Qt6::Widgets)
  add_executable(
    editor-canvas-tests
    editor-canvas-tests.cpp
    "${VOID_RADIO_SRC}/ui/editor-canvas.cpp"
    "${VOID_RADIO_SRC}/ui/editor-canvas.hpp"
  )
  set_target_properties(editor-canvas-tests PROPERTIES AUTOMOC ON)
  target_link_libraries(editor-canvas-tests PRIVATE void-radio-testable Qt6::Widgets)
  add_test(NAME editor-canvas COMMAND editor-canvas-tests)
  set_tests_properties(editor-canvas PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# The headless libobs smoke test: built when libobs can be found, and run
# against the plugin named by VOID_RADIO_PLUGIN. See obs-smoke-test.cpp.
find_package(libobs QUIET)

if(TARGET OBS::libobs)
  # How the widget's layout, style and line settings resolve, and the layout
  # editor's guarantees. Links the plugin sources it needs against libobs.
  add_executable(
    widget-design-tests
    widget-design-tests.cpp
    "${VOID_RADIO_SRC}/render/bar-renderer.cpp"
    "${VOID_RADIO_SRC}/sources/music-widget/widget-defaults.cpp"
    "${VOID_RADIO_SRC}/sources/music-widget/widget-design.cpp"
    "${VOID_RADIO_SRC}/sources/music-widget/widget-draw.cpp"
    "${VOID_RADIO_SRC}/sources/music-widget/widget-layout.cpp"
    "${VOID_RADIO_SRC}/sources/music-widget/widget-palette.cpp"
  )
  target_include_directories(
    widget-design-tests
    PRIVATE "${VOID_RADIO_SRC}" "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/obs-stubs"
  )
  target_link_libraries(widget-design-tests PRIVATE OBS::libobs)
  add_test(NAME widget-design COMMAND widget-design-tests)

  add_executable(obs-smoke-test obs-smoke-test.cpp)
  target_link_libraries(obs-smoke-test PRIVATE media-fixtures OBS::libobs)
  target_include_directories(obs-smoke-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

  set(VOID_RADIO_PLUGIN "" CACHE FILEPATH "Built plugin module for the libobs smoke test")

  if(VOID_RADIO_PLUGIN)
    add_test(NAME obs-smoke COMMAND obs-smoke-test)
    set_tests_properties(
      obs-smoke
      PROPERTIES
        ENVIRONMENT "VOID_RADIO_PLUGIN=${VOID_RADIO_PLUGIN};VOID_RADIO_DATA=${CMAKE_CURRENT_SOURCE_DIR}/../data"
    )
  endif()
endif()
