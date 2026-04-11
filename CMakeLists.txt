cmake_minimum_required(VERSION 3.14)
project(cvffmpeg VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------
option(CVFFMPEG_BUILD_EXAMPLES "Build example programs" OFF)
option(CVFFMPEG_BUILD_TESTS "Build tests" OFF)

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
    src/LogLevel.cpp
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
# Tests
# ------------------------------------------------------------------------------
if(CVFFMPEG_BUILD_TESTS)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.0
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)

    # Generate test fixture videos at configure time
    find_program(FFMPEG_EXECUTABLE ffmpeg)
    find_program(FFPROBE_EXECUTABLE ffprobe)

    if(FFMPEG_EXECUTABLE)
        set(TEST_FIXTURES_DIR "${CMAKE_CURRENT_BINARY_DIR}/test_fixtures")
        file(MAKE_DIRECTORY ${TEST_FIXTURES_DIR})

        # 8-bit BT.709 limited range, 1280x720, 3 frames
        execute_process(
            COMMAND ${FFMPEG_EXECUTABLE} -y -f lavfi -i "color=c=red:size=1280x720:rate=30:duration=0.1"
                -c:v libx264 -pix_fmt yuv420p
                -colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range tv
                "${TEST_FIXTURES_DIR}/bt709_limited.mp4"
            OUTPUT_QUIET ERROR_QUIET
        )

        # 8-bit BT.709 full range, 320x240, 3 frames
        execute_process(
            COMMAND ${FFMPEG_EXECUTABLE} -y -f lavfi -i "color=c=blue:size=320x240:rate=30:duration=0.1"
                -c:v libx264 -pix_fmt yuv420p
                -colorspace bt709 -color_primaries bt709 -color_trc bt709 -color_range pc
                "${TEST_FIXTURES_DIR}/bt709_full.mp4"
            OUTPUT_QUIET ERROR_QUIET
        )

        # SD content (480p) for BT.601 auto-detection
        execute_process(
            COMMAND ${FFMPEG_EXECUTABLE} -y -f lavfi -i "color=c=green:size=640x480:rate=30:duration=0.1"
                -c:v libx264 -pix_fmt yuv420p
                "${TEST_FIXTURES_DIR}/sd_480p.mp4"
            OUTPUT_QUIET ERROR_QUIET
        )

        # 10-bit HDR (BT.2020 + PQ) - only if libx265 is available
        execute_process(
            COMMAND ${FFMPEG_EXECUTABLE} -y -f lavfi -i "color=c=yellow:size=1920x1080:rate=30:duration=0.1"
                -c:v libx265 -pix_fmt yuv420p10le
                -colorspace bt2020nc -color_primaries bt2020 -color_trc smpte2084 -color_range tv
                -tag:v hvc1
                "${TEST_FIXTURES_DIR}/hdr10.mp4"
            RESULT_VARIABLE HDR_FIXTURE_RESULT
            OUTPUT_QUIET ERROR_QUIET
        )
        if(HDR_FIXTURE_RESULT EQUAL 0)
            set(HAVE_HDR_FIXTURE TRUE)
        else()
            set(HAVE_HDR_FIXTURE FALSE)
            message(STATUS "libx265 not available, HDR reader test fixture skipped")
        endif()
    else()
        message(WARNING "ffmpeg not found, test fixtures will not be generated")
    endif()

    add_executable(cvffmpeg_tests
        tests/test_video_reader.cpp
        tests/test_video_writer.cpp
        tests/test_roundtrip.cpp
    )

    target_link_libraries(cvffmpeg_tests PRIVATE cvffmpeg Catch2::Catch2WithMain)

    target_compile_definitions(cvffmpeg_tests PRIVATE
        TEST_FIXTURES_DIR="${TEST_FIXTURES_DIR}"
    )

    if(FFPROBE_EXECUTABLE)
        target_compile_definitions(cvffmpeg_tests PRIVATE
            FFPROBE_EXECUTABLE="${FFPROBE_EXECUTABLE}"
        )
    endif()

    if(HAVE_HDR_FIXTURE)
        target_compile_definitions(cvffmpeg_tests PRIVATE HAVE_HDR_FIXTURE)
    endif()

    add_test(NAME cvffmpeg_tests COMMAND cvffmpeg_tests)
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
