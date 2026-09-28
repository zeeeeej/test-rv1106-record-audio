cmake_minimum_required(VERSION 3.16)

project(hd_record_audio VERSION 1.0.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HD_RECORD_AUDIO_BUILD_RECORDER "Build the RV1106 recorder executable" ON)

add_library(hd_record_core
    src/audio_frame.cpp
    src/cli.cpp
    src/wav_writer.cpp
)
target_include_directories(hd_record_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(hd_record_core PRIVATE -Wall -Wextra -Wpedantic)

if(HD_RECORD_AUDIO_BUILD_RECORDER)
    set(RK_MPI_ROOT "" CACHE PATH "Path containing RK MPI include/ and lib/uclibc/")
    if(NOT RK_MPI_ROOT)
        message(FATAL_ERROR "RK_MPI_ROOT is required when HD_RECORD_AUDIO_BUILD_RECORDER=ON")
    endif()

    find_library(ROCKIT_LIBRARY
        NAMES rockit
        PATHS "${RK_MPI_ROOT}/lib/uclibc"
        NO_DEFAULT_PATH
        REQUIRED
    )

    add_executable(hd_record_audio
        src/main.cpp
        src/rk_audio_recorder.cpp
    )
    target_include_directories(hd_record_audio PRIVATE "${RK_MPI_ROOT}/include")
    target_link_libraries(hd_record_audio PRIVATE hd_record_core "${ROCKIT_LIBRARY}" pthread)
    set_target_properties(hd_record_audio PROPERTIES
        BUILD_RPATH "/oem/usr/lib"
        INSTALL_RPATH "/oem/usr/lib"
    )
    target_link_options(hd_record_audio PRIVATE
        "-Wl,-rpath-link,${RK_MPI_ROOT}/lib/uclibc"
    )
    target_compile_options(hd_record_audio PRIVATE -Wall -Wextra -Wpedantic)

    install(TARGETS hd_record_audio RUNTIME DESTINATION bin)
    install(FILES config/config_aivqe_anr.json DESTINATION share/hd-record-audio)
endif()

if(BUILD_TESTING)
    add_executable(hd_record_audio_tests tests/unit_tests.cpp)
    target_link_libraries(hd_record_audio_tests PRIVATE hd_record_core)
    target_compile_options(hd_record_audio_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME hd_record_audio_tests COMMAND hd_record_audio_tests)
endif()
