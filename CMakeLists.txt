cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

# A binary only loads in the OBS major line whose SDK it was built against
# (OBS 33 removed the gs_image_file4 API), so make sure the SDK matches the
# release files being produced.
if(OBS_SDK_VARIANT STREQUAL "obs33" AND libobs_VERSION VERSION_LESS 33)
  message(FATAL_ERROR "OBS_SDK_VARIANT=obs33 requires the OBS 33+ SDK, found libobs ${libobs_VERSION}")
elseif(NOT OBS_SDK_VARIANT AND libobs_VERSION VERSION_GREATER_EQUAL 33)
  message(
    WARNING
    "Building the default release against libobs ${libobs_VERSION}. "
    "The result only loads in OBS Studio 33+; use an *-obs33 preset to label it as such."
  )
endif()

# FFmpeg decodes the music and reads its tags. OBS ships it on every platform;
# see cmake/common/FindFFmpeg.cmake for where it comes from.
find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec avutil swresample)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil FFmpeg::swresample)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core Gui)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/audio/audio-decoder.cpp
    src/audio/audio-decoder.hpp
    src/audio/audio-format.hpp
    src/audio/beat-detector.cpp
    src/audio/beat-detector.hpp
    src/audio/deck.hpp
    src/audio/ffmpeg-util.cpp
    src/audio/loudness.cpp
    src/audio/loudness.hpp
    src/audio/ffmpeg-util.hpp
    src/audio/ring-buffer.cpp
    src/audio/ring-buffer.hpp
    src/audio/stream-deck.cpp
    src/audio/stream-deck.hpp
    src/filters/music-info-filter.cpp
    src/filters/music-info-filter.hpp
    src/filters/music-reactive-filter.cpp
    src/filters/music-reactive-filter.hpp
    src/integrations/websocket.cpp
    src/integrations/websocket.hpp
    src/music/album-art.cpp
    src/music/album-art.hpp
    src/music/command-queue.hpp
    src/music/ducker.cpp
    src/music/ducker.hpp
    src/music/gain-envelope.cpp
    src/music/gain-envelope.hpp
    src/music/library-worker.cpp
    src/music/library-worker.hpp
    src/music/loudness-scan.cpp
    src/music/loudness-scan.hpp
    src/music/music-api.cpp
    src/music/music-api.hpp
    src/music/music-link.cpp
    src/music/music-source-duck.cpp
    src/music/music-source-info.cpp
    src/music/music-source-settings.cpp
    src/music/music-source.cpp
    src/music/music-source.hpp
    src/music/player.cpp
    src/music/player.hpp
    src/music/playlist.cpp
    src/music/playlist.hpp
    src/music/snapshot.hpp
    src/music/source-hotkeys.cpp
    src/music/source-hotkeys.hpp
    src/music/track-tags.cpp
    src/music/track-tags.hpp
    src/plugin-main.cpp
    src/render/bar-renderer.cpp
    src/render/bar-renderer.hpp
    src/render/image-file-compat.hpp
    src/sources/progress-bar-source.cpp
    src/sources/progress-bar-source.hpp
    src/util/text-template.cpp
    src/util/text-template.hpp
    src/util/vr-util.cpp
    src/util/vr-util.hpp
)

# The Music Widget draws its own glyphs and decodes its own cover art, neither
# of which libobs can do, so it needs Qt. Without it the plugin still builds; it
# just has no widget source.
if(ENABLE_QT)
  target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      src/render/image-util.cpp
      src/render/image-util.hpp
      src/render/text-render.cpp
      src/render/text-render.hpp
      src/sources/music-widget-source.cpp
      src/sources/music-widget-source.hpp
      src/sources/music-widget/art-loader.cpp
      src/sources/music-widget/art-loader.hpp
      src/sources/music-widget/widget-defaults.cpp
      src/sources/music-widget/widget-design.cpp
      src/sources/music-widget/widget-design.hpp
      src/sources/music-widget/spectrum.cpp
      src/sources/music-widget/spectrum.hpp
      src/sources/music-widget/widget-draw.cpp
      src/sources/music-widget/widget-extras.cpp
      src/sources/music-widget/widget-motion.cpp
      src/sources/music-widget/widget-palette.cpp
      src/sources/music-widget/widget-palette.hpp
      src/sources/music-widget/widget-visualizer.cpp
      src/sources/music-widget/widget-layout.cpp
      src/sources/music-widget/widget-layout.hpp
      src/sources/music-widget/widget-properties.cpp
      src/sources/music-widget/widget-render.cpp
      src/sources/music-widget/widget-settings.cpp
      src/sources/music-widget/widget.hpp
  )
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE VOID_RADIO_ENABLE_WIDGET)
endif()

# The Now Playing dock needs both Qt and the frontend API. Without them the
# plugin still builds; it just has no dock.
if(ENABLE_QT AND ENABLE_FRONTEND_API)
  target_sources(
    ${CMAKE_PROJECT_NAME}
    PRIVATE
      src/ui/editor-canvas.cpp
      src/ui/editor-canvas.hpp
      src/ui/editor-snap.cpp
      src/ui/editor-snap.hpp
      src/ui/now-playing-dock.cpp
      src/ui/now-playing-dock.hpp
      src/ui/widget-editor.cpp
      src/ui/widget-editor.hpp
  )
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE VOID_RADIO_ENABLE_DOCK VOID_RADIO_ENABLE_EDITOR)
endif()

if(OS_WINDOWS)
  # Keep the windows.h min/max macros away from std::min and std::max.
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE NOMINMAX)
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
