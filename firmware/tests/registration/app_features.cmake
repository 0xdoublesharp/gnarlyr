echo_add_unit_test(NAME test_theme_palette
    SOURCES app/test_theme_palette.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

foreach(feature_test cassette_ui moments_screen player_cassette mixtape_store mixtape_ui personal_tags discovery_screen discovery_preferences)
    echo_add_unit_test(NAME test_${feature_test}
        SOURCES app/test_${feature_test}.c app/fake_hal.c
        LIBRARIES echo_app echo_library_fixtures
        PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
    )
endforeach()

echo_add_unit_test(NAME test_listening_store
    SOURCES app/test_listening_store.c playback/listening_fixture.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${CMAKE_CURRENT_SOURCE_DIR}/playback
)

echo_add_unit_test(NAME test_configured_controls
    SOURCES app/test_configured_controls.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

echo_add_unit_test(NAME test_resume_comfort
    SOURCES app/test_resume_comfort.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

echo_add_unit_test(NAME test_plug_modes
    SOURCES app/test_plug_modes.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

echo_add_unit_test(NAME test_usb_chooser
    SOURCES app/test_usb_chooser.c app/fake_hal.c support/screen_snapshot.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${CMAKE_CURRENT_SOURCE_DIR}/support
)

echo_add_unit_test(NAME test_portrait_settings_layout
    SOURCES app/test_portrait_settings_layout.c app/fake_hal.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)

if(UNIX AND NOT APPLE)
    target_link_libraries(test_equalizer PRIVATE m)
    target_link_libraries(test_equalizer_response PRIVATE m)
endif()

echo_add_unit_test(NAME test_clock_screen
    SOURCES app/test_clock_screen.c app/fake_hal.c support/text_spy.c support/screen_snapshot.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${PROJECT_SOURCE_DIR}/src/gfx
)

echo_add_unit_test(NAME test_lyrics_screen
    SOURCES app/test_lyrics_screen.c app/fake_hal.c support/text_spy.c support/screen_snapshot.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${PROJECT_SOURCE_DIR}/src/gfx
)

echo_add_unit_test(NAME test_smart_list_screen
    SOURCES app/test_smart_list_screen.c app/fake_hal.c playback/listening_fixture.c
        support/text_spy.c support/screen_snapshot.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${PROJECT_SOURCE_DIR}/src/gfx
        ${CMAKE_CURRENT_SOURCE_DIR}/playback
)

echo_add_unit_test(NAME test_listening_screen
    SOURCES app/test_listening_screen.c app/fake_hal.c playback/listening_fixture.c
        support/text_spy.c support/screen_snapshot.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${PROJECT_SOURCE_DIR}/src/gfx
        ${CMAKE_CURRENT_SOURCE_DIR}/playback
)

echo_add_unit_test(NAME test_working_motion
    SOURCES app/test_working_motion.c app/fake_hal.c support/text_spy.c support/screen_snapshot.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${PROJECT_SOURCE_DIR}/src/gfx
)

echo_add_unit_test(NAME test_set_clock
    SOURCES app/test_set_clock.c app/fake_hal.c support/text_spy.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${PROJECT_SOURCE_DIR}/src/gfx
)
echo_add_unit_test(NAME test_clock_write
    SOURCES app/test_clock_write.c ${PROJECT_SOURCE_DIR}/src/app/extension_calls.c
    LIBRARIES echo_app
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app
)
echo_add_unit_test(NAME test_cassette_flip
    SOURCES app/test_cassette_flip.c app/fake_hal.c support/text_spy.c
    LIBRARIES echo_app echo_library_fixtures
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/app ${PROJECT_SOURCE_DIR}/src/gfx
)
