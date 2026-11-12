set(ECHO_WMA_DIR ${PROJECT_SOURCE_DIR}/third_party/rockbox_wma)
add_library(echo_wma STATIC
    ${ECHO_WMA_DIR}/codecs/libwma/wmadeci.c
    ${ECHO_WMA_DIR}/codecs/libwma/wmafixed.c
    ${ECHO_WMA_DIR}/codecs/lib/fft-ffmpeg.c
    ${ECHO_WMA_DIR}/codecs/lib/mdct.c
    ${ECHO_WMA_DIR}/codecs/lib/mdct_lookup.c
)
target_include_directories(echo_wma SYSTEM PUBLIC
    ${ECHO_WMA_DIR}/port ${ECHO_WMA_DIR} ${ECHO_WMA_DIR}/codecs/lib ${ECHO_WMA_DIR}/codecs/libwma
)
target_compile_options(echo_wma PRIVATE
    -include ${ECHO_WMA_DIR}/port/platform.h
    -w -fwrapv -fno-strict-aliasing -fno-sanitize=shift
)
target_link_libraries(echo_wma PRIVATE echo_options)

add_library(echo_wma_tables STATIC wma_vlc.c)
target_include_directories(echo_wma_tables SYSTEM PRIVATE
    ${ECHO_WMA_DIR}/port ${ECHO_WMA_DIR}/codecs/lib
)
target_link_libraries(echo_wma_tables PRIVATE echo_options)
target_link_libraries(echo_wma PUBLIC echo_wma_tables)
