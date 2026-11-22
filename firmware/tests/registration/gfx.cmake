echo_add_unit_test(NAME test_color SOURCES gfx/test_color.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_rect SOURCES gfx/test_rect.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_art_codec SOURCES gfx/test_art_codec.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_canvas SOURCES gfx/test_canvas.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_image SOURCES gfx/test_image.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_strip_renderer SOURCES gfx/test_strip_renderer.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_frame_diff SOURCES gfx/test_frame_diff.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_wireframe SOURCES gfx/test_wireframe.c LIBRARIES echo_gfx)
if(UNIX)
    target_link_libraries(test_wireframe PRIVATE m)
endif()
echo_add_unit_test(NAME test_font SOURCES gfx/test_font.c gfx/tiny_font.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_text SOURCES gfx/test_text.c gfx/tiny_font.c LIBRARIES echo_gfx)
echo_add_unit_test(NAME test_font_pack
    SOURCES gfx/test_font_pack.c gfx/tiny_font.c support/test_pack.c support/test_glyph_face.c
    LIBRARIES echo_gfx
)
echo_add_unit_test(NAME test_fonts SOURCES fonts/test_fonts.c LIBRARIES echo_fonts)
add_executable(art_codec_tool gfx/art_codec_tool.c)
target_link_libraries(art_codec_tool PRIVATE echo_gfx echo_options)
add_test(NAME art_codec_parity
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/tests/test_art_codec.py --tool $<TARGET_FILE:art_codec_tool>)
add_test(NAME art_codec_bench
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/art_codec_bench.py --check)
