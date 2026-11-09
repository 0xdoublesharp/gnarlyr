add_library(echo_options INTERFACE)

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
target_compile_definitions(echo_board INTERFACE PANEL_ECHO_MADCTL=${ECHO_PANEL_MADCTL})
if(ECHO_BOOT_TRACE)
    target_compile_definitions(echo_board INTERFACE ECHO_BOOT_TRACE)
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

add_library(echo_lto INTERFACE)
if(CMAKE_CROSSCOMPILING)
    target_compile_options(echo_lto INTERFACE -flto -ffat-lto-objects -g)
endif()
