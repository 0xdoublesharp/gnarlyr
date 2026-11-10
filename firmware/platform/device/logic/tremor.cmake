set(ECHO_TREMOR_DIR ${PROJECT_SOURCE_DIR}/third_party/tremor)
set(ECHO_TREMOR_SOURCES
    ${ECHO_TREMOR_DIR}/bitwise.c
    ${ECHO_TREMOR_DIR}/codebook.c
    ${ECHO_TREMOR_DIR}/dsp.c
    ${ECHO_TREMOR_DIR}/floor0.c
    ${ECHO_TREMOR_DIR}/floor1.c
    ${ECHO_TREMOR_DIR}/floor_lookup.c
    ${ECHO_TREMOR_DIR}/info.c
    ${ECHO_TREMOR_DIR}/mapping0.c
    ${ECHO_TREMOR_DIR}/mdct.c
    ${ECHO_TREMOR_DIR}/res012.c
)
add_library(echo_tremor STATIC ${ECHO_TREMOR_SOURCES})
target_include_directories(echo_tremor SYSTEM PUBLIC ${ECHO_TREMOR_DIR})
target_include_directories(echo_tremor PRIVATE include)
target_compile_options(echo_tremor PRIVATE -w -fwrapv -funsigned-char -fno-strict-aliasing)
set_source_files_properties(${ECHO_TREMOR_SOURCES} PROPERTIES COMPILE_OPTIONS -fno-sanitize=shift)
target_link_libraries(echo_tremor PRIVATE echo_options)
