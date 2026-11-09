cmake_minimum_required(VERSION 3.25)
project(echo_recovery LANGUAGES CXX)

if(NOT DEFINED RKDEVELOPTOOL_SOURCE)
    message(FATAL_ERROR "Set RKDEVELOPTOOL_SOURCE to the patched pinned source")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/verify_source.py ${RKDEVELOPTOOL_SOURCE}
    RESULT_VARIABLE source_check
)
if(NOT source_check EQUAL 0)
    message(FATAL_ERROR "Recovery source verification failed")
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/config.h" "#define PACKAGE_VERSION \"1.32\"\n")
add_executable(rkdeveloptool-echo
    ${RKDEVELOPTOOL_SOURCE}/main.cpp
    ${RKDEVELOPTOOL_SOURCE}/crc.cpp
    ${RKDEVELOPTOOL_SOURCE}/RKBoot.cpp
    ${RKDEVELOPTOOL_SOURCE}/RKComm.cpp
    ${RKDEVELOPTOOL_SOURCE}/RKDevice.cpp
    ${RKDEVELOPTOOL_SOURCE}/RKImage.cpp
    ${RKDEVELOPTOOL_SOURCE}/RKLog.cpp
    ${RKDEVELOPTOOL_SOURCE}/RKScan.cpp
)
target_include_directories(rkdeveloptool-echo PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR} ${RKDEVELOPTOOL_SOURCE})
target_compile_options(rkdeveloptool-echo PRIVATE
    -Wall -Wextra -Wreturn-type -fno-strict-aliasing -Wno-vla-cxx-extension)
target_compile_definitions(rkdeveloptool-echo PRIVATE _FILE_OFFSET_BITS=64 _LARGE_FILE)
target_link_libraries(rkdeveloptool-echo PRIVATE PkgConfig::LIBUSB iconv)
