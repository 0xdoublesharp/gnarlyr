echo_add_unit_test(NAME test_screen_stack SOURCES ui/test_screen_stack.c LIBRARIES echo_ui)
echo_add_unit_test(NAME test_navigation SOURCES ui/test_navigation.c LIBRARIES echo_ui)
echo_add_unit_test(NAME test_marquee SOURCES ui/test_marquee.c LIBRARIES echo_ui)
echo_add_unit_test(NAME test_fade SOURCES ui/test_fade.c LIBRARIES echo_ui)
echo_add_unit_test(NAME test_slide SOURCES ui/test_slide.c LIBRARIES echo_ui)
echo_add_unit_test(NAME test_cover_flow SOURCES ui/test_cover_flow.c LIBRARIES echo_ui)
echo_add_unit_test(NAME test_placeholder_art
    SOURCES ui/test_placeholder_art.c
    LIBRARIES echo_ui echo_fonts
)
echo_add_unit_test(NAME test_label
    SOURCES ui/test_label.c gfx/tiny_font.c
    LIBRARIES echo_ui
    PRIVATE_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/gfx
)

add_library(echo_ui_fixtures STATIC gfx/tiny_font.c ui/ui_test_theme.c ui/ui_test_canvas.c)
target_include_directories(echo_ui_fixtures PUBLIC gfx ui)
target_link_libraries(echo_ui_fixtures PUBLIC echo_ui PRIVATE echo_options)

foreach(ui_test theme_layout list_view list_navigation fast_scroll header menu_screen message_screen
        choice_screen toast)
    echo_add_unit_test(NAME test_${ui_test}
        SOURCES ui/test_${ui_test}.c
        LIBRARIES echo_ui_fixtures
    )
endforeach()

echo_add_unit_test(NAME test_status_bar
    SOURCES ui/test_status_bar.c
    LIBRARIES echo_ui_fixtures echo_fonts
)
