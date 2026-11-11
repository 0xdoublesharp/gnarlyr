echo_add_unit_test(NAME test_cover_grid
    SOURCES image/test_cover_grid.c
    LIBRARIES echo_image
)
add_executable(cover_decode_tool image/cover_decode_tool.c)
target_link_libraries(cover_decode_tool PRIVATE echo_image echo_options)
