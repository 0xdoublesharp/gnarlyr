add_library(echo_app STATIC
    app.c
    cassette_art.c
    modules/listening_module.c
    screens/listening_screen.c
    screens/moments_screen.c
    screens/lyrics_screen.c
    screens/listening_work.c
    screens/smart_list_screen.c
    discovery_screen.c
    discovery_text.c
    discovery_preferences.c
    discovery_playback.c
    order_publish.c
    order_adopt.c
    personal_tags.c
    player_storage.c
    player_queue.c
    queue_edits.c
    queue_actions.c
    mixtape_store.c
    mixtape_ui.c
    cassette_ui.c
    mixtape_screen.c
    button_settings.c
    home_editor.c
    theme_palette.c
    listening_store.c
    card_files.c
    app_screens.c
    bluetooth_link.c
    bluetooth_link_ui.c
    bluetooth_store.c
    board_layout.c
    browse_screen.c
    cover_cache.c
    cover_placeholders.c
    default_theme.c
    default_theme_metrics.c
    favorite_playlist.c
    favorites_store.c
    diagnostics_values.c
    diagnostics_rows.c
    draw_profile.c
    equalizer_presets.c
    frame_overlay.c
    home_art.c
    language.c
    language_list.c
    library_rescan.c
    music_check.c
    music_walk.c
    library_store.c
    modules/extension_module.c
    modules/library_module.c
    modules/settings_module.c
    modules/views_module.c
    modules/playing_module.c
    player.c
    screen_modules.c
    settings_preview.c
    settings_screens.c
    screens/set_clock_screen.c
    settings_store.c
    headphone_store.c
    headphone_parameters.c
    headphone_store_path.c
    headphone_store_edit.c
    headphone_editor.c
    spoken_store.c
    wall_time.c
    screens/bluetooth_screen.c
    screens/card_format_screen.c
    screens/calendar_screen.c
    screens/cassette_flip_screen.c
    screens/clock_screen.c
    screens/charge_screen.c
    screens/charge_art.c
    screens/cover_flow_screen.c
    screens/equalizer_screen.c
    screens/equalizer_browser.c
    screens/equalizer_plot.c
    screens/now_playing_screen.c
    screens/up_next_screen.c
)
if(ECHO_RELEASE)
    target_sources(echo_app PRIVATE diagnostics_menu_release.c)
else()
    target_sources(echo_app PRIVATE diagnostics_menu.c debug_loop.c debug_runner.c
        debug_screenshot.c debug_steps.c modules/debug_module.c)
endif()
if(ECHO_BUILD_DEVICE)
    target_sources(echo_app PRIVATE extension_calls.c)
    target_compile_definitions(echo_app PRIVATE ECHO_USB_OVERLAY_RENDERER)
else()
    target_sources(echo_app PRIVATE modules/linked_modules.c library_rescan_job.c
        screens/usb_dac_menu.c screens/usb_screen.c
        screens/usb_screen_dac.c)
endif()
target_include_directories(echo_app PUBLIC include PRIVATE .)
target_link_libraries(echo_app
    PUBLIC echo_core echo_gfx echo_ui echo_fonts echo_library echo_playback echo_settings echo_hal
    PRIVATE echo_options echo_compact echo_lto
)
