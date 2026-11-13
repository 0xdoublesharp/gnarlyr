add_library(echo_options INTERFACE)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(echo_options INTERFACE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE=1)
endif()

target_compile_options(echo_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wcast-qual
    -Wpointer-arith
    -Wundef
    -Wvla
    -Wdouble-promotion
    -Wimplicit-fallthrough
    -Wformat=2
)

target_compile_definitions(echo_options INTERFACE ${FIRMWARE_DEFINITIONS})

add_library(echo_board INTERFACE)
if(NOT ECHO_RELEASE)
    target_compile_definitions(echo_board INTERFACE ECHO_DEBUG_LOOP)
    if(ECHO_USB_SCREEN_CAPTURES)
        target_compile_definitions(echo_board INTERFACE ECHO_USB_SCREEN_CAPTURES)
    endif()
endif()
target_link_libraries(echo_options INTERFACE echo_board)

add_library(echo_compact INTERFACE)
if(CMAKE_CROSSCOMPILING)
    target_compile_options(echo_compact INTERFACE -Os)
endif()

if(ECHO_SANITIZE)
    target_compile_options(echo_options INTERFACE
        -fsanitize=undefined
        -fno-sanitize-recover=all
        -fno-omit-frame-pointer
    )
    target_link_options(echo_options INTERFACE -fsanitize=undefined)
endif()

if(ECHO_ADDRESS_SANITIZER)
    target_compile_options(echo_options INTERFACE -fsanitize=address)
    target_link_options(echo_options INTERFACE -fsanitize=address)
endif()

if(ECHO_BUILD_FUZZERS)
    target_compile_options(echo_options INTERFACE -fsanitize=fuzzer-no-link)
endif()

add_library(echo_lto INTERFACE)
if(CMAKE_CROSSCOMPILING)
    target_compile_options(echo_lto INTERFACE -flto -ffat-lto-objects -g)
endif()
