cmake_minimum_required(VERSION 3.14)
project(cvffmpeg VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------
option(CVFFMPEG_BUILD_EXAMPLES "Build example programs" OFF)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------
find_package(OpenCV 4.0 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavformat>=58.29   # FFmpeg 4.2+
    libavcodec>=58.54
    libswscale>=5.5
    libavutil>=56.31
)

# Create an imported target for FFmpeg (PkgConfig doesn't always do this cleanly)
add_library(cvffmpeg_ffmpeg INTERFACE IMPORTED)
target_include_directories(cvffmpeg_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(cvffmpeg_ffmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})

# Handle macOS framework flags from pkg-config
set(_framework_flags "")
set(_other_flags "")
list(LENGTH FFMPEG_LDFLAGS_OTHER _list_len)
set(_i 0)
while(_i LESS _list_len)
    list(GET FFMPEG_LDFLAGS_OTHER ${_i} _flag)
    if("${_flag}" STREQUAL "-framework")
        math(EXPR _next "${_i} + 1")
        if(_next LESS _list_len)
            list(GET FFMPEG_LDFLAGS_OTHER ${_next} _fw)
            list(APPEND _framework_flags "-framework ${_fw}")
            math(EXPR _i "${_i} + 2")
            continue()
        endif()
    endif()
    list(APPEND _other_flags "${_flag}")
    math(EXPR _i "${_i} + 1")
endwhile()

target_link_libraries(cvffmpeg_ffmpeg INTERFACE ${_other_flags} ${_framework_flags} ${FFMPEG_LIBRARIES})

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------
add_library(cvffmpeg
    src/VideoReader.cpp
    src/VideoWriter.cpp
)

add_library(cvffmpeg::cvffmpeg ALIAS cvffmpeg)

target_include_directories(cvffmpeg
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(cvffmpeg
    PUBLIC
        ${OpenCV_LIBS}
        cvffmpeg_ffmpeg
)

set_target_properties(cvffmpeg PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME cvffmpeg
)

# ------------------------------------------------------------------------------
# Examples
# ------------------------------------------------------------------------------
if(CVFFMPEG_BUILD_EXAMPLES)
    add_executable(compare_readers examples/compare_readers.cpp)
    target_link_libraries(compare_readers PRIVATE cvffmpeg)
endif()

# ------------------------------------------------------------------------------
# Install
# ------------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS cvffmpeg
    EXPORT cvffmpegTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/cvffmpeg
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT cvffmpegTargets
    FILE cvffmpegTargets.cmake
    NAMESPACE cvffmpeg::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cvffmpeg
)

configure_package_config_file(
    cmake/cvffmpegConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/cvffmpegConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cvffmpeg
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cvffmpegConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cvffmpegConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cvffmpegConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/cvffmpeg
)
