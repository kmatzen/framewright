cmake_minimum_required(VERSION 3.14)
project(framewright VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------
option(FRAMEWRIGHT_BUILD_EXAMPLES "Build example programs" OFF)
option(FRAMEWRIGHT_BUILD_TESTS "Build tests" OFF)
option(FRAMEWRIGHT_BUILD_PYTHON "Build Python bindings" OFF)

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
add_library(framewright_ffmpeg INTERFACE IMPORTED)
target_include_directories(framewright_ffmpeg INTERFACE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(framewright_ffmpeg INTERFACE ${FFMPEG_LIBRARY_DIRS})

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

target_link_libraries(framewright_ffmpeg INTERFACE ${_other_flags} ${_framework_flags} ${FFMPEG_LIBRARIES})

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------
add_library(framewright
    src/LogLevel.cpp
    src/VideoReader.cpp
    src/VideoWriter.cpp
)

add_library(framewright::framewright ALIAS framewright)

target_include_directories(framewright
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(framewright
    PUBLIC
        ${OpenCV_LIBS}
        framewright_ffmpeg
)

set_target_properties(framewright PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME framewright
    # The Python extension is a shared module that links this library. On ELF
    # targets, static objects built without -fPIC cannot go into a .so:
    #   relocation R_X86_64_PC32 ... can not be used when making a shared object
    # Mach-O does not have this restriction, so omitting it fails on Linux only.
    POSITION_INDEPENDENT_CODE ON
)

# ------------------------------------------------------------------------------
# Examples
# ------------------------------------------------------------------------------
if(FRAMEWRIGHT_BUILD_EXAMPLES)
    add_executable(compare_readers examples/compare_readers.cpp)
    target_link_libraries(compare_readers PRIVATE framewright)

    add_executable(basic_read examples/basic_read.cpp)
    target_link_libraries(basic_read PRIVATE framewright)

    add_executable(basic_write examples/basic_write.cpp)
    target_link_libraries(basic_write PRIVATE framewright)

    add_executable(hdr_write examples/hdr_write.cpp)
    target_link_libraries(hdr_write PRIVATE framewright)
endif()

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
if(FRAMEWRIGHT_BUILD_TESTS)
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

        # Seek fixture: 60 frames, each with a distinct luma value (16 + 3*N)
        # so a decoded frame can be identified by content, and keyframes forced
        # to every 30 frames so seek()'s keyframe path is reachable. The step is
        # chosen so the last frame stays inside the limited range ceiling of
        # 235 -- larger steps clip and make the final frames indistinguishable.
        execute_process(
            COMMAND ${FFMPEG_EXECUTABLE} -y -f lavfi
                -i "color=c=black:s=64x64:r=30:d=2,format=yuv420p,geq=lum='16+3*N':cb='128':cr='128'"
                -c:v libx264 -qp 0 -g 30 -keyint_min 30 -sc_threshold 0 -pix_fmt yuv420p
                "${TEST_FIXTURES_DIR}/seek_numbered.mp4"
            RESULT_VARIABLE SEEK_FIXTURE_RESULT
            OUTPUT_QUIET ERROR_QUIET
        )
        if(SEEK_FIXTURE_RESULT EQUAL 0)
            set(HAVE_SEEK_FIXTURE TRUE)
            message(STATUS "seek fixture: enabled")
        else()
            set(HAVE_SEEK_FIXTURE FALSE)
            message(STATUS "seek fixture: DISABLED, seek tests will not run")
        endif()

        # Same content encoded with B-frames, so the decoder's reorder buffer
        # is exercised. -qp 0 makes x264 emit all I/P regardless of -bf, hence
        # the explicit x264-params and a lossy-but-near-transparent CRF; the
        # luma step of 3 survives it comfortably.
        execute_process(
            COMMAND ${FFMPEG_EXECUTABLE} -y -f lavfi
                -i "color=c=black:s=64x64:r=30:d=2,format=yuv420p,geq=lum='16+3*N':cb='128':cr='128'"
                -c:v libx264 -crf 12 -g 30 -keyint_min 30 -sc_threshold 0
                -x264-params "bframes=2:b-adapt=0" -pix_fmt yuv420p
                "${TEST_FIXTURES_DIR}/seek_numbered_bframes.mp4"
            RESULT_VARIABLE SEEK_BF_FIXTURE_RESULT
            OUTPUT_QUIET ERROR_QUIET
        )
        if(SEEK_BF_FIXTURE_RESULT EQUAL 0)
            set(HAVE_SEEK_BFRAME_FIXTURE TRUE)
            message(STATUS "seek B-frame fixture: enabled")
        else()
            set(HAVE_SEEK_BFRAME_FIXTURE FALSE)
            message(STATUS "seek B-frame fixture: DISABLED, reordering tests will not run")
        endif()

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
            message(STATUS "HDR fixture: enabled")
        else()
            set(HAVE_HDR_FIXTURE FALSE)
            message(STATUS "HDR fixture: DISABLED, libx265 not available")
        endif()
    else()
        message(WARNING "ffmpeg not found, test fixtures will not be generated")
    endif()

    add_executable(framewright_tests
        tests/test_video_reader.cpp
        tests/test_video_writer.cpp
        tests/test_roundtrip.cpp
    )

    target_link_libraries(framewright_tests PRIVATE framewright Catch2::Catch2WithMain)

    target_compile_definitions(framewright_tests PRIVATE
        TEST_FIXTURES_DIR="${TEST_FIXTURES_DIR}"
    )

    if(FFPROBE_EXECUTABLE)
        target_compile_definitions(framewright_tests PRIVATE
            FFPROBE_EXECUTABLE="${FFPROBE_EXECUTABLE}"
        )
    endif()

    if(HAVE_HDR_FIXTURE)
        target_compile_definitions(framewright_tests PRIVATE HAVE_HDR_FIXTURE)
    endif()

    if(HAVE_SEEK_FIXTURE)
        target_compile_definitions(framewright_tests PRIVATE HAVE_SEEK_FIXTURE)
    endif()

    if(HAVE_SEEK_BFRAME_FIXTURE)
        target_compile_definitions(framewright_tests PRIVATE HAVE_SEEK_BFRAME_FIXTURE)
    endif()

    add_test(NAME framewright_tests COMMAND framewright_tests)
endif()

# ------------------------------------------------------------------------------
# Python bindings
# ------------------------------------------------------------------------------
if(FRAMEWRIGHT_BUILD_PYTHON)
    find_package(pybind11 CONFIG REQUIRED)

    pybind11_add_module(_framewright python/bindings.cpp)
    target_link_libraries(_framewright PRIVATE framewright)
    install(TARGETS _framewright DESTINATION .)
endif()

# ------------------------------------------------------------------------------
# Install
# ------------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS framewright
    EXPORT framewrightTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/framewright
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT framewrightTargets
    FILE framewrightTargets.cmake
    NAMESPACE framewright::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/framewright
)

configure_package_config_file(
    cmake/framewrightConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/framewrightConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/framewright
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/framewrightConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/framewrightConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/framewrightConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/framewright
)
