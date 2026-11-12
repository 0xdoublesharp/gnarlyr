echo_add_unit_test(NAME test_cover_grid
    SOURCES image/test_cover_grid.c
    LIBRARIES echo_image
)
echo_add_unit_test(NAME test_jpeg_inverse
    SOURCES image/test_jpeg_inverse.c
    LIBRARIES echo_image
    PRIVATE_INCLUDES ${PROJECT_SOURCE_DIR}/src/image
)
add_executable(cover_decode_tool image/cover_decode_tool.c)
target_link_libraries(cover_decode_tool PRIVATE echo_image echo_options)
