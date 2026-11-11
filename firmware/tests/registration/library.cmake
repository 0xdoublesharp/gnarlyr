foreach(library_test album_order block_cache library library_content library_browse library_view favorites covers sort_key track_fields playlist_reader cue_sheet music_fingerprint lyrics)
    echo_add_unit_test(NAME test_${library_test}
        SOURCES library/test_${library_test}.c
        LIBRARIES echo_library_fixtures
    )
endforeach()
add_library(echo_reference_builder STATIC
    library/reference/reference_builder.c
    library/reference/reference_builder_encode.c
    library/reference/reference_builder_model.c
    library/reference/reference_builder_paths.c
)
target_include_directories(echo_reference_builder PUBLIC library/reference)
target_link_libraries(echo_reference_builder PUBLIC echo_library_scan PRIVATE echo_options)

add_library(echo_library_checks STATIC library/library_compare.c library/random_library.c)
target_include_directories(echo_library_checks PUBLIC library)
target_link_libraries(echo_library_checks PUBLIC echo_library_scan PRIVATE echo_options)

echo_add_unit_test(NAME test_library_builder
    SOURCES library/test_library_builder.c
    LIBRARIES echo_library_fixtures echo_library_checks echo_reference_builder
)
set_tests_properties(test_library_builder PROPERTIES TIMEOUT 600)
target_compile_definitions(test_library_builder PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_library_replay_gain
    SOURCES library/test_library_replay_gain.c
    LIBRARIES echo_library_fixtures
)
echo_add_unit_test(NAME test_replay_gain
    SOURCES library/test_replay_gain.c
    LIBRARIES echo_library_scan
)
if(UNIX)
    target_link_libraries(test_replay_gain PRIVATE m)
endif()
add_executable(replay_gain_tag_tool library/replay_gain_tag_tool.c)
target_link_libraries(replay_gain_tag_tool PRIVATE echo_library_scan echo_options)
add_test(NAME replay_gain_metadata_formats
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/library/check_replay_gain_tags.py
        --reader $<TARGET_FILE:replay_gain_tag_tool>
        --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/tags
)
echo_add_unit_test(NAME test_track_tags
    SOURCES library/test_track_tags.c
    LIBRARIES echo_library_fixtures
)
target_compile_definitions(test_track_tags PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_mp4_chapters
    SOURCES library/test_mp4_chapters.c
    LIBRARIES echo_library_fixtures echo_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/library
)
target_compile_definitions(test_mp4_chapters PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
echo_add_unit_test(NAME test_cover_build
    SOURCES library/test_cover_build.c
    LIBRARIES echo_library_scan
)
target_compile_definitions(test_cover_build PRIVATE ECHO_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
