add_library(echo_playback STATIC
    bookmarks.c
    listening_export.c
    listening_log.c
    listening_stats.c
    listening_track.c
    mix_catalog.c
    mix_catalog_path.c
    mix_identity.c
    mix_resolve.c
    mix_tape.c
    play_queue.c
    resume_state.c
    session_journal.c
    spoken.c
    store.c
    track_order.c
    up_next.c
)
target_include_directories(echo_playback PUBLIC include)
target_link_libraries(echo_playback
    PUBLIC echo_core echo_library
    PRIVATE echo_options echo_compact echo_lto
)
