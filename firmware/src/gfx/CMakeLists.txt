add_library(echo_gfx STATIC
    art_codec.c
    canvas.c
    color.c
    font.c
    font_pack.c
    frame_diff.c
    image.c
    image_quad.c
    rect.c
    strip_renderer.c
    text.c
    text_style.c
    wireframe.c
)
target_include_directories(echo_gfx PUBLIC include)
if(CMAKE_CROSSCOMPILING)
    set_source_files_properties(canvas.c color.c font.c font_pack.c frame_diff.c rect.c strip_renderer.c
        text.c text_style.c wireframe.c PROPERTIES COMPILE_OPTIONS -Os)
endif()
target_link_libraries(echo_gfx
    PUBLIC echo_core
    PRIVATE echo_options echo_lto
)
