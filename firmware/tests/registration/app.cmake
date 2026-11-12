echo_add_unit_test(NAME test_language
    SOURCES app/test_language.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures echo_test_pack
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

echo_add_unit_test(NAME test_app_language
    SOURCES app/test_app_language.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures echo_test_pack
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

echo_add_unit_test(NAME test_app_modules
    SOURCES app/test_app_modules.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_card_format_ui
    SOURCES app/test_card_format_ui.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/card_format_captures)
add_test(NAME card_format_layout_captures
    COMMAND test_card_format_ui --captures ${CMAKE_CURRENT_BINARY_DIR}/card_format_captures
)
echo_add_unit_test(NAME test_home_screen
    SOURCES app/test_home_screen.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_screen_modules
    SOURCES app/test_screen_modules.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

echo_add_unit_test(NAME test_pseudo_language
    SOURCES app/test_pseudo_language.c app/fake_hal.c support/text_spy.c
    LIBRARIES echo_app echo_library_fixtures echo_test_pack
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${PROJECT_SOURCE_DIR}/src/gfx
)
target_compile_definitions(test_pseudo_language PRIVATE ECHO_PSEUDO_PACK="${ECHO_PSEUDO_PACK}")
add_dependencies(test_pseudo_language echo_test_language_packs)

echo_add_unit_test(NAME test_equalizer_browser
    SOURCES app/test_equalizer_browser.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

echo_add_unit_test(NAME test_equalizer_screen
    SOURCES app/test_equalizer_screen.c
    LIBRARIES echo_app
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

echo_add_unit_test(NAME test_usb_screen
    SOURCES app/test_usb_screen.c app/fake_hal.c support/text_spy.c support/screen_snapshot.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${PROJECT_SOURCE_DIR}/src/gfx
)
echo_add_unit_test(NAME test_usb_dac_menu
    SOURCES app/test_usb_dac_menu.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_charge_screen
    SOURCES app/test_charge_screen.c app/fake_hal.c support/text_spy.c support/screen_snapshot.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${PROJECT_SOURCE_DIR}/src/gfx
)

echo_add_unit_test(NAME test_english_layout
    SOURCES app/test_english_layout.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

echo_add_unit_test(NAME test_app
    SOURCES app/test_app.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_release_diagnostics
    SOURCES app/test_release_diagnostics.c ${PROJECT_SOURCE_DIR}/src/app/diagnostics_menu_release.c
        app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_draw_profile
    SOURCES app/test_draw_profile.c
    LIBRARIES echo_app
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_player
    SOURCES app/test_player.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_player_cue
    SOURCES app/test_player_cue.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_player_transitions
    SOURCES app/test_player_transitions.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_spoken_word
    SOURCES app/test_spoken_word.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_cover_cache
    SOURCES app/test_cover_cache.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_scan_guard
    SOURCES app/test_scan_guard.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_stores
    SOURCES app/test_stores.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_headphone_store
    SOURCES app/test_headphone_store.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_headphone_editor
    SOURCES app/test_headphone_editor.c app/fake_hal.c support/text_spy.c support/screen_snapshot.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${PROJECT_SOURCE_DIR}/src/gfx
)
echo_add_unit_test(NAME test_browse_screen
    SOURCES app/test_browse_screen.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_up_next_screen
    SOURCES app/test_up_next_screen.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_cover_placeholders
    SOURCES app/test_cover_placeholders.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_cover_flow_order
    SOURCES app/test_cover_flow_order.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_icons
    SOURCES ui/test_icons.c
    LIBRARIES echo_app
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
add_executable(icon_sheet ui/icon_sheet.c)
target_link_libraries(icon_sheet PRIVATE echo_ui echo_options)
add_test(NAME icon_sheet_renders COMMAND icon_sheet ${CMAKE_BINARY_DIR}/icon_sheet.png)
echo_add_unit_test(NAME test_board_layout
    SOURCES app/test_board_layout.c
    LIBRARIES echo_app
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_now_playing_screen
    SOURCES app/test_now_playing_screen.c app/fake_hal.c
    LIBRARIES echo_app echo_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_diagnostics_values
    SOURCES app/test_diagnostics_values.c app/fake_hal.c
    LIBRARIES echo_app echo_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_bluetooth_link
    SOURCES app/test_bluetooth_link.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_bluetooth_screen
    SOURCES app/test_bluetooth_screen.c app/fake_hal.c
    LIBRARIES echo_app echo_library_scan
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

echo_add_unit_test(NAME test_quick_settings
    SOURCES app/test_quick_settings.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

echo_add_unit_test(NAME test_quick_settings_screen
    SOURCES app/test_quick_settings_screen.c app/fake_hal.c support/text_spy.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${PROJECT_SOURCE_DIR}/src/gfx
)

echo_add_unit_test(NAME test_check_progress
    SOURCES app/test_check_progress.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

echo_add_unit_test(NAME test_music_walk_progress
    SOURCES app/test_music_walk_progress.c ${PROJECT_SOURCE_DIR}/src/app/music_walk.c
    LIBRARIES echo_core echo_library_scan echo_hal echo_gfx
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${PROJECT_SOURCE_DIR}/src/app/include
)
