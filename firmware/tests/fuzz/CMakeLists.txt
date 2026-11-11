add_library(echo_fuzz_support STATIC fuzz_source.c)
target_include_directories(echo_fuzz_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/library/include
)
target_link_libraries(echo_fuzz_support PUBLIC echo_options)

function(echo_add_fuzzer name)
    cmake_parse_arguments(FUZZ "" "" "LIBRARIES;SOURCES;PRIVATE_INCLUDES" ${ARGN})
    add_executable(fuzz_${name} fuzz_${name}.c ${FUZZ_SOURCES})
    target_include_directories(fuzz_${name} PRIVATE ${FUZZ_PRIVATE_INCLUDES})
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
echo_add_fuzzer(discovery_screen
    SOURCES ../app/fake_hal.c ../library/memory_source.c ../library/fixture_library.c
    LIBRARIES echo_app echo_library_scan
    PRIVATE_INCLUDES
        ${PROJECT_SOURCE_DIR}/src/app
        ${PROJECT_SOURCE_DIR}/tests/app
        ${PROJECT_SOURCE_DIR}/tests/library
)
target_compile_definitions(fuzz_discovery_screen PRIVATE
    ECHO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)
echo_add_fuzzer(personal_tags
    SOURCES ../app/fake_hal.c ../library/memory_source.c ../library/fixture_library.c
    LIBRARIES echo_app echo_library_scan
    PRIVATE_INCLUDES
        ${PROJECT_SOURCE_DIR}/src/app
        ${PROJECT_SOURCE_DIR}/tests/app
        ${PROJECT_SOURCE_DIR}/tests/library
)
target_compile_definitions(fuzz_personal_tags PRIVATE
    ECHO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)
echo_add_fuzzer(browse_screen
    SOURCES ../app/fake_hal.c ../library/memory_source.c ../library/fixture_library.c
    LIBRARIES echo_app echo_library_scan
    PRIVATE_INCLUDES
        ${PROJECT_SOURCE_DIR}/src/app
        ${PROJECT_SOURCE_DIR}/tests/app
        ${PROJECT_SOURCE_DIR}/tests/library
)
target_compile_definitions(fuzz_browse_screen PRIVATE
    ECHO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)
echo_add_fuzzer(app_navigation
    SOURCES ../app/fake_hal.c ../library/memory_source.c ../library/fixture_library.c
    LIBRARIES echo_app echo_library_scan
    PRIVATE_INCLUDES
        ${PROJECT_SOURCE_DIR}/src/app
        ${PROJECT_SOURCE_DIR}/tests/app
        ${PROJECT_SOURCE_DIR}/tests/library
)
target_compile_definitions(fuzz_app_navigation PRIVATE
    ECHO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)
echo_add_fuzzer(library_builder
    SOURCES ../library/memory_source.c
    LIBRARIES echo_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/tests/library
)
echo_add_fuzzer(cover_image LIBRARIES echo_image)
echo_add_fuzzer(placeholder_art LIBRARIES echo_ui echo_fonts)
echo_add_fuzzer(play_queue
    SOURCES ../library/memory_source.c ../library/fixture_library.c
    LIBRARIES echo_playback echo_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/tests/library
)
target_compile_definitions(fuzz_play_queue PRIVATE
    ECHO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)
echo_add_fuzzer(mp3_probe LIBRARIES echo_device_logic)
echo_add_fuzzer(opus_decode LIBRARIES echo_device_logic)
echo_add_fuzzer(fat_volume LIBRARIES echo_device_logic)
