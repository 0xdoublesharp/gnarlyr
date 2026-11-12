add_library(echo_playback STATIC
    bookmarks.c
    cassette_budget.c
    listening_export.c
    listening_clear.c
    listening_cursor.c
    listening_log.c
    listening_recap.c
    listening_stats.c
    listening_track.c
    moments.c
    mix_catalog.c
    mix_catalog_path.c
    mix_identity.c
    mix_resolve.c
    mix_tape.c
    play_queue.c
    resume_rewind.c
    resume_state.c
    session_journal.c
    smart_list.c
    spoken.c
    spoken_shelf.c
    store.c
    track_keys.c
    track_keys_path.c
    track_order.c
    track_transition.c
    up_next.c
)
target_include_directories(echo_playback PUBLIC include)
target_link_libraries(echo_playback
    PUBLIC echo_core echo_library
    PRIVATE echo_options echo_compact echo_lto
)
