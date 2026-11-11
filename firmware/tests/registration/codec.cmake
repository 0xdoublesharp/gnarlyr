add_executable(ogg_packet_tool codec/ogg_packet_tool.c)
target_link_libraries(ogg_packet_tool PRIVATE echo_codec echo_options)
echo_add_unit_test(NAME test_ogg_packet
    SOURCES codec/test_ogg_packet.c
    LIBRARIES echo_codec
)
target_compile_definitions(test_ogg_packet PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_alac
    SOURCES codec/test_alac.c
    LIBRARIES echo_codec
)
target_compile_definitions(test_alac PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_flac
    SOURCES codec/test_flac.c
    LIBRARIES echo_codec
)
target_compile_definitions(test_flac PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_flac_bits
    SOURCES codec/test_flac_bits.c
    LIBRARIES echo_codec
)
target_include_directories(test_flac_bits PRIVATE ${PROJECT_SOURCE_DIR}/src/codec)
