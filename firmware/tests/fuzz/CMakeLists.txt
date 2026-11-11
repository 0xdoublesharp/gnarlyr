add_library(echo_fuzz_support STATIC fuzz_source.c)
target_include_directories(echo_fuzz_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/library/include
)
target_link_libraries(echo_fuzz_support PUBLIC echo_options)

function(echo_add_fuzzer name)
    cmake_parse_arguments(FUZZ "" "" "LIBRARIES" ${ARGN})
    add_executable(fuzz_${name} fuzz_${name}.c)
    target_link_libraries(fuzz_${name} PRIVATE echo_fuzz_support echo_options ${FUZZ_LIBRARIES})
    target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
endfunction()

echo_add_fuzzer(track_tags LIBRARIES echo_library_scan)
echo_add_fuzzer(playlist_reader LIBRARIES echo_library_scan)
echo_add_fuzzer(session_journal LIBRARIES echo_playback)
echo_add_fuzzer(library_database LIBRARIES echo_library)
echo_add_fuzzer(mp4_demux LIBRARIES echo_device_logic)
echo_add_fuzzer(store LIBRARIES echo_playback)
echo_add_fuzzer(settings_codec LIBRARIES echo_settings)
echo_add_fuzzer(cue_sheet LIBRARIES echo_library_scan)
