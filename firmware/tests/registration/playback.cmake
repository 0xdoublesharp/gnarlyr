foreach(playback_test play_queue resume_rewind resume_state up_next bookmarks spoken store track_order track_transition session_journal mix_catalog mix_tape mix_resolve store_roots)
    echo_add_unit_test(NAME test_${playback_test}
        SOURCES playback/test_${playback_test}.c
        LIBRARIES echo_playback echo_library_fixtures
    )
endforeach()

foreach(listening_test listening_log listening_stats listening_export)
    echo_add_unit_test(NAME test_${listening_test}
        SOURCES playback/test_${listening_test}.c playback/listening_fixture.c
        LIBRARIES echo_playback
    )
endforeach()
echo_add_unit_test(NAME test_listening_track
    SOURCES playback/test_listening_track.c
    LIBRARIES echo_playback echo_library_fixtures
)
echo_add_unit_test(NAME test_track_keys
    SOURCES playback/test_track_keys.c playback/keys_disk.c
    LIBRARIES echo_playback echo_library_fixtures
)
echo_add_unit_test(NAME test_smart_list
    SOURCES playback/test_smart_list.c playback/keys_disk.c playback/listening_fixture.c
    LIBRARIES echo_playback
)
