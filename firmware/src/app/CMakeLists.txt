add_library(echo_app STATIC
    app.c
    card_files.c
    app_screens.c
    bluetooth_link.c
    bluetooth_store.c
    board_layout.c
    browse_screen.c
    cover_cache.c
    default_theme.c
    favorite_playlist.c
    favorites_store.c
    diagnostics_values.c
    diagnostics_rows.c
    $<IF:$<BOOL:${ECHO_RELEASE}>,diagnostics_menu_release.c,diagnostics_menu.c>
    draw_profile.c
    equalizer_presets.c
    frame_overlay.c
    home_art.c
    library_rescan.c
    music_check.c
    library_store.c
    player.c
    settings_screens.c
    settings_store.c
    spoken_store.c
    screens/bluetooth_screen.c
    screens/charge_screen.c
    screens/cover_flow_screen.c
    screens/equalizer_screen.c
    screens/now_playing_screen.c
    screens/up_next_screen.c
)
if(NOT ECHO_RELEASE)
    target_sources(echo_app PRIVATE debug_loop.c debug_runner.c debug_screenshot.c)
endif()
target_include_directories(echo_app PUBLIC include PRIVATE .)
target_link_libraries(echo_app
    PUBLIC echo_core echo_gfx echo_ui echo_fonts echo_library echo_playback echo_settings echo_hal
    PRIVATE echo_options echo_compact echo_lto
)
