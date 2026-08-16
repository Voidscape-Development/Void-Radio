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

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
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
    src/filters/music-info-filter.cpp
    src/filters/music-info-filter.hpp
    src/music/music-api.hpp
    src/music/music-source.cpp
    src/music/music-source.hpp
    src/music/playlist.cpp
    src/music/playlist.hpp
    src/music/snapshot.hpp
    src/music/track-tags.cpp
    src/music/track-tags.hpp
    src/plugin-main.cpp
    src/sources/progress-bar-source.cpp
    src/sources/progress-bar-source.hpp
    src/util/text-template.cpp
    src/util/text-template.hpp
    src/util/vr-util.cpp
    src/util/vr-util.hpp
)

# The Now Playing dock needs both Qt and the frontend API. Without them the
# plugin still builds; it just has no dock.
if(ENABLE_QT AND ENABLE_FRONTEND_API)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/ui/now-playing-dock.cpp src/ui/now-playing-dock.hpp)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE VOID_RADIO_ENABLE_DOCK)
endif()

if(OS_WINDOWS)
  # Keep the windows.h min/max macros away from std::min and std::max.
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE NOMINMAX)
endif()

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
