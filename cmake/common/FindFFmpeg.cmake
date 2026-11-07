# FindFFmpeg: locates the FFmpeg libraries the plugin decodes audio with.
#
# OBS ships FFmpeg on every platform, and the plugin links the same libraries
# OBS loads, so nothing extra is installed alongside it:
#
# * Windows and macOS: the obs-deps package fetched by the buildspec carries
#   FFmpeg's headers and import libraries, and is already on CMAKE_PREFIX_PATH.
# * Linux: the distribution's FFmpeg development packages (libavcodec-dev and
#   friends), the same FFmpeg OBS itself is built against there.
#
# Usage: find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec avutil swresample)
# Defines an imported target FFmpeg::<component> for each component found.

include(FindPackageHandleStandardArgs)

find_package(PkgConfig QUIET)

set(_ffmpeg_required_vars)

foreach(_component IN LISTS FFmpeg_FIND_COMPONENTS)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(PC_FFmpeg_${_component} QUIET lib${_component})
  endif()

  find_path(
    FFmpeg_${_component}_INCLUDE_DIR
    NAMES lib${_component}/${_component}.h
    HINTS ${PC_FFmpeg_${_component}_INCLUDE_DIRS}
    PATH_SUFFIXES include
  )

  find_library(
    FFmpeg_${_component}_LIBRARY
    NAMES ${_component} lib${_component}
    HINTS ${PC_FFmpeg_${_component}_LIBRARY_DIRS}
    PATH_SUFFIXES lib
  )

  mark_as_advanced(FFmpeg_${_component}_INCLUDE_DIR FFmpeg_${_component}_LIBRARY)

  if(FFmpeg_${_component}_INCLUDE_DIR AND FFmpeg_${_component}_LIBRARY)
    set(FFmpeg_${_component}_FOUND TRUE)

    if(NOT TARGET FFmpeg::${_component})
      add_library(FFmpeg::${_component} UNKNOWN IMPORTED)
      set_target_properties(
        FFmpeg::${_component}
        PROPERTIES
          IMPORTED_LOCATION "${FFmpeg_${_component}_LIBRARY}"
          INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_${_component}_INCLUDE_DIR}"
      )
    endif()
  else()
    set(FFmpeg_${_component}_FOUND FALSE)
  endif()

  list(APPEND _ffmpeg_required_vars FFmpeg_${_component}_INCLUDE_DIR FFmpeg_${_component}_LIBRARY)
endforeach()

find_package_handle_standard_args(FFmpeg REQUIRED_VARS ${_ffmpeg_required_vars} HANDLE_COMPONENTS)

unset(_ffmpeg_required_vars)
unset(_component)
