set(ECHO_DEMAC_DIR ${PROJECT_SOURCE_DIR}/third_party/demac)
add_library(echo_demac STATIC
    ${ECHO_DEMAC_DIR}/crc.c
    ${ECHO_DEMAC_DIR}/decoder.c
    ${ECHO_DEMAC_DIR}/entropy.c
    ${ECHO_DEMAC_DIR}/predictor.c
    ${ECHO_DEMAC_DIR}/filter_16_11.c
    ${ECHO_DEMAC_DIR}/filter_32_10.c
    ${ECHO_DEMAC_DIR}/filter_64_11.c
    ${ECHO_DEMAC_DIR}/filter_256_13.c
    ${ECHO_DEMAC_DIR}/filter_1280_15.c
)
target_include_directories(echo_demac SYSTEM PUBLIC ${ECHO_DEMAC_DIR})
target_compile_options(echo_demac PRIVATE -w -fwrapv -fno-strict-aliasing -fno-sanitize=shift)
target_link_libraries(echo_demac PRIVATE echo_options)
